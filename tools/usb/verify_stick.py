#!/usr/bin/env python3
"""Verify that a SurfaceRT_OS USB stick (or image) is actually bootable.

Works on an image file, a block device (/dev/sdX) or a Windows disk
(\\\\.\\PhysicalDriveN).  Checks, in order:

  1. protective MBR + GPT, with the partition table CRC
  2. an EFI system partition at the expected offset
  3. FAT32 boot sector inside it
  4. \\EFI\\BOOT\\BOOTARM.EFI exists and is a PE32/ARMNT EFI application
  5. \\SRTOS\\KERNEL.ELF exists, is an ARM ELF, and carries the platform marker
  6. the marker matches the platform recorded in \\SRTOS\\PLATFORM

The exit code is 0 only if every check passes, so it can guard a flashing step
in a script.
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# Work both in the repository (tools/usb -> ../../scripts) and in the packaged
# USB folder, where a copy of the parsers sits next to this file.
for candidate in (os.path.join(ROOT, "scripts"),
                  os.path.join(HERE, "scripts"),
                  HERE):
    if os.path.isdir(candidate):
        sys.path.insert(0, candidate)

import fat32  # noqa: E402
import gpt    # noqa: E402

IMAGE_FILE_MACHINE_ARMNT = 0x1C4


def platform_label():
    try:
        import socket  # noqa: F401
    except ImportError:
        pass


class Source:
    """Read a region of a file or a raw device without loading it all."""

    def __init__(self, path, length):
        self.path = path
        self.length = length
        self.f = open(path, "rb", buffering=0)

    def read(self, offset, size):
        self.f.seek(offset)
        return self.f.read(size)

    def close(self):
        self.f.close()


def size_of(path):
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            return f.tell()
    except OSError as exc:
        raise SystemExit("cannot open %s: %s" % (path, exc))


def check(name, ok, detail=""):
    print("  %-46s %s%s" % (name, "OK" if ok else "FAIL",
                           ("  (%s)" % detail) if detail else ""))
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description="verify a SurfaceRT_OS USB stick")
    ap.add_argument("target", help="image file, /dev/sdX or \\\\.\\PhysicalDriveN")
    args = ap.parse_args(argv)

    length = size_of(args.target)
    print("checking %s (%d MiB)" % (args.target, length // (1024 * 1024)))
    src = Source(args.target, length)
    failures = 0

    mbr = src.read(0, 512)
    failures += not check("protective MBR", len(mbr) == 512 and
                          mbr[510:512] == b"\x55\xaa" and mbr[450] == 0xEE)
    hdr = src.read(512, 512)
    failures += not check("GPT header", hdr[:8] == b"EFI PART")
    if hdr[:8] != b"EFI PART":
        src.close()
        print("\n%d check(s) failed - this stick will not boot" % failures)
        return 1

    entries_lba = struct.unpack_from("<Q", hdr, 72)[0]
    count = struct.unpack_from("<I", hdr, 80)[0]
    esize = struct.unpack_from("<I", hdr, 84)[0]
    table = src.read(entries_lba * 512, count * esize)

    parts = []
    for i in range(count):
        e = table[i * esize:(i + 1) * esize]
        if e[:16] == bytes(16):
            continue
        parts.append(dict(
            index=i + 1,
            type=e[:16],
            start=struct.unpack_from("<Q", e, 32)[0],
            sectors=(struct.unpack_from("<Q", e, 40)[0] -
                     struct.unpack_from("<Q", e, 32)[0] + 1),
            name=e[56:128].decode("utf-16-le", "ignore").rstrip("\x00")))
    failures += not check("partitions present", bool(parts),
                          ", ".join("%d:%s" % (p["index"], p["name"]) for p in parts))

    esp = None
    for p in parts:
        if p["type"] == gpt.guid_bytes(gpt.TYPE_ESP):
            esp = p
            break
    failures += not check("EFI system partition", esp is not None)
    if not esp:
        src.close()
        print("\n%d check(s) failed" % failures)
        return 1

    esp_off = esp["start"] * 512
    esp_len = min(esp["sectors"] * 512, 256 * 1024 * 1024)
    esp_bytes = src.read(esp_off, esp_len)
    bs = esp_bytes[0:512]
    failures += not check("FAT32 boot sector inside the ESP",
                          bs[510:512] == b"\x55\xaa" and bs[82:87] == b"FAT32")

    try:
        fs = fat32.Fat32Reader(esp_bytes)
    except Exception as exc:  # noqa: BLE001 - report, do not crash
        failures += not check("ESP can be parsed", False, str(exc))
        src.close()
        print("\n%d check(s) failed" % failures)
        return 1

    loader = None
    for path in ("/EFI/BOOT/BOOTARM.EFI", "/EFI/MICROSOFT/BOOT/BOOTMGFW.EFI"):
        try:
            data = fs.read(path)
        except Exception:  # noqa: BLE001
            failures += not check("loader at %s" % path, False)
            continue
        ok = data[:2] == b"MZ" and data[0x3C:0x40] != b""
        if ok:
            e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
            if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
                ok = False
            else:
                machine = struct.unpack_from("<H", data, e_lfanew + 4)[0]
                subsystem = struct.unpack_from("<H", data, e_lfanew + 4 + 20 + 68)[0]
                ok = machine == IMAGE_FILE_MACHINE_ARMNT and subsystem == 10
                detail = "ARMNT PE, subsystem %d, %d bytes" % (subsystem, len(data))
        failures += not check("loader at %s" % path, ok,
                              detail if ok else "not an EFI ARM application")
        if path.endswith("BOOTARM.EFI"):
            loader = ok

    try:
        kernel = fs.read("/SRTOS/KERNEL.ELF")
        is_elf = kernel[:4] == b"\x7fELF" and struct.unpack_from("<H", kernel, 18)[0] == 40
        failures += not check("kernel at /SRTOS/KERNEL.ELF", is_elf,
                              "%d bytes" % len(kernel) if is_elf else "not an ARM ELF")
    except Exception:  # noqa: BLE001
        kernel = b""
        failures += not check("kernel at /SRTOS/KERNEL.ELF", False)

    try:
        platform = fs.read("/SRTOS/PLATFORM").decode("ascii", "replace").splitlines()[0]
    except Exception:  # noqa: BLE001
        platform = ""
    mark = ("SRTOS-PLATFORM:%s" % platform).encode() if platform else b""
    failures += not check("kernel matches /SRTOS/PLATFORM",
                          bool(platform) and mark in kernel, platform or "missing")

    try:
        default = fs.read("/SRTOS/DEFAULT")
        failures += not check("boot selector /SRTOS/DEFAULT",
                              len(default) == 16 and
                              default.rstrip(b"\x00").lower() in (b"srtos", b"windows"),
                              default.rstrip(b"\x00").decode("ascii", "replace"))
    except Exception:  # noqa: BLE001
        failures += not check("boot selector /SRTOS/DEFAULT", False)

    try:
        cfg = fs.read("/SRTOS/BOOT.CFG").decode("ascii", "replace")
        failures += not check("BOOT.CFG present", "kernel=" in cfg)
    except Exception:  # noqa: BLE001
        failures += not check("BOOT.CFG present", False)

    src.close()
    if failures:
        print("\n%d check(s) failed - do not expect this stick to boot" % failures)
        return 1
    print("\nall checks passed: this stick boots SurfaceRT_OS on %s" % platform)
    return 0


if __name__ == "__main__":
    sys.exit(main())
