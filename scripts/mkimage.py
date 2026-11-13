#!/usr/bin/env python3
"""Build bootable SurfaceRT_OS images - no external tools required.

Produces, in --out:

  BOOTARM.EFI        the UEFI loader (also copied to EFI/Microsoft/Boot)
  esp.img            FAT32 EFI system partition, 40 MiB by default
  srtos-disk.img     GPT disk image for the internal eMMC (install target)
  srtos-usb.img      GPT USB image: ESP + INSTALL partition with the disk image
  MANIFEST.txt       sizes and SHA-256 of every artefact

Write the USB image with:

  sudo dd if=dist/srtos-usb.img of=/dev/sdX bs=4M conv=fsync status=progress
"""

import argparse
import hashlib
import os
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fat32
import gpt
from platforms import PLATFORMS, DEFAULT_PLATFORM

MIB = 1024 * 1024
ROOT = Path(__file__).resolve().parent.parent

BOOT_CFG = """# SurfaceRT_OS boot configuration - read by BOOTARM.EFI
#
# kernel      name of the kernel ELF inside \\SRTOS on this partition
# kernel_base preferred physical load address (the loader relocates anyway)
# cmdline     passed to the kernel as one string
# timeout     seconds to wait before booting (0 = boot immediately)

kernel=KERNEL.ELF
kernel_base=0x82000000
cmdline=console=fb,uart loglevel=info
"""

def boot_cfg(platform):
    soc = PLATFORMS[platform]
    return (BOOT_CFG +
            "#\n# built for: %s\n# %s, %s\n# timer: %s, panel %s\n"
            "platform=%s\n" % (soc["board"], soc["soc"], soc["cpu"],
                                 soc["timer"], soc["panel"], platform))


README_INSTALL = """SurfaceRT_OS installation media
================================

1. Boot the tablet from this stick (hold volume-down while powering on, or use
the firmware boot menu).

2. In the serial console run:

     install /install/srtos-disk.img 1

   The '1' is the confirmation: without it the installer refuses to touch the
   internal eMMC.

3. Power cycle.  The tablet now boots SRTOS from its own storage.
"""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def mount_into(image, start_lba, partition_image):
    off = start_lba * 512
    image[off:off + len(partition_image)] = partition_image


def build_esp(build_dir, size, loader, kernel, modules, platform=DEFAULT_PLATFORM):
    fs = fat32.Fat32Builder(size, cluster_bytes=512, label="SRTOSESP")
    fs.mkdir("/EFI/BOOT")
    fs.add_file("/EFI/BOOT/BOOTARM.EFI", loader)
    # The Surface RT firmware always looks for this path first; keeping a copy
    # makes the stick work on stock firmware and on a boot-menu selection.
    fs.mkdir("/EFI/MICROSOFT/BOOT")
    fs.add_file("/EFI/MICROSOFT/BOOT/BOOTMGFW.EFI", loader)
    fs.mkdir("/SRTOS")
    fs.add_file("/SRTOS/KERNEL.ELF", kernel)
    fs.add_file("/SRTOS/BOOT.CFG", boot_cfg(platform).encode())
    soc = PLATFORMS[platform]
    fs.add_file("/SRTOS/PLATFORM",
                ("%s\n%s\n%s\n%s\n" %
                 (platform, soc["board"], soc["soc"], soc["cpu"])).encode())
    # fixed 16 byte selector: the kernel's shell rewrites it in place, which is
    # why FAT writes of the same size are all it needs
    fs.add_file("/SRTOS/DEFAULT", b"srtos".ljust(16, b"\x00"))
    fs.add_file("/SRTOS/VERSION", ("SurfaceRT_OS 1.0.0 (Tegra Sink)\n"
                                   "built %s\n" % time.strftime("%Y-%m-%d %H:%M:%S")
                                   ).encode())
    fs.mkdir("/SERVICES")
    for name, data in modules.items():
        fs.add_file("/SERVICES/%s" % name, data)
    return fs.build()


def build_disk(esp, data_size, esp_size):
    total = esp_size + data_size
    total_sectors = total // 512
    esp_sectors = esp_size // 512
    data_start = esp_sectors
    image = bytearray(total)
    gpt.build(image, [
        dict(start=34, sectors=esp_sectors - 34, type=gpt.TYPE_ESP,
             name="EFI System Partition"),
        dict(start=data_start, sectors=total_sectors - 33 - data_start,
             type=gpt.TYPE_LINUX_DATA, name="SRTOS-DATA"),
    ])
    mount_into(image, 34, esp)
    return bytes(image)



def emit_usb_package(out_dir, platform, image_path):
    """Copy the flashing/verification tooling next to the image.

    The result is a folder someone can copy to a Windows machine (or use in
    place on Linux) and get from "raw image" to "bootable stick" without
    reading the whole repository.
    """
    pkg = Path(out_dir) / "usb"
    pkg.mkdir(parents=True, exist_ok=True)
    shutil.copy2(image_path, pkg / "srtos-usb.img")
    for name in ("flash-windows.ps1", "flash-linux.sh", "device-check.ps1",
                 "install-esp.ps1", "verify_stick.py"):
        shutil.copy2(ROOT / "tools" / "usb" / name, pkg / name)
    # the verifier reuses the parsers instead of re-implementing FAT32/GPT
    (pkg / "scripts").mkdir(exist_ok=True)
    for name in ("fat32.py", "gpt.py"):
        shutil.copy2(ROOT / "scripts" / name, pkg / "scripts" / name)
    shutil.copy2(ROOT / "docs" / "UNLOCK-AND-BOOT.md", pkg / "UNLOCK-AND-BOOT.md")

    soc = PLATFORMS[platform]
    readme = """SurfaceRT_OS USB package
========================

Board   : %(board)s
SoC     : %(soc)s (%(cpu)s)
Platform: %(platform)s
Image   : srtos-usb.img (%(size)d MiB, SHA-256 in SHA256SUMS.txt)

1. Flash the image (this ERASES the stick):

   Windows (elevated PowerShell):
       powershell -ExecutionPolicy Bypass -File flash-windows.ps1

   Linux / macOS:
       sudo ./flash-linux.sh srtos-usb.img

   Both tools only offer removable disks, refuse the system disk, read the
   image back and finally run verify_stick.py.  A stick that passes can only
   fail to boot because of the tablet's boot policy.

2. Check the tablet (copy device-check.ps1 to it and run in PowerShell):

       powershell -ExecutionPolicy Bypass -File device-check.ps1

   Expected before unlocking: "Secure Boot ... ON".

3. If Secure Boot is on, follow UNLOCK-AND-BOOT.md.  Short version:
   Golden Keys (Test Mode) -> Yahallo (turns Secure Boot off permanently).

4. Boot: power off, insert the stick, hold Volume-Down, press Power, release
   Volume-Down at the Surface logo, pick the USB device.

Optional: keep Windows and boot SRTOS from the internal ESP with
install-esp.ps1 -Source <stick drive>:  (see UNLOCK-AND-BOOT.md step 6b).

Verify at any time:
    python3 verify_stick.py <image or device>
""" % dict(board=soc["board"], soc=soc["soc"], cpu=soc["cpu"],
           platform=platform, size=os.path.getsize(image_path) // MIB)
    (pkg / "README.txt").write_text(readme)

    digest = hashlib.sha256((pkg / "srtos-usb.img").read_bytes()).hexdigest()
    (pkg / "SHA256SUMS.txt").write_text(
        "%s  srtos-usb.img\n" % digest)
    return pkg


def main(argv=None):
    ap = argparse.ArgumentParser(description="build SurfaceRT_OS images")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--esp-size", type=int, default=40 * MIB)
    ap.add_argument("--data-size", type=int, default=96 * MIB)
    ap.add_argument("--install-size", type=int, default=224 * MIB)
    ap.add_argument("--usb-package", action="store_true",
                    help="also emit a self-contained usb/ folder with flashers,"
                         " the verifier, the device check and the unlock guide")
    ap.add_argument("--platform", default=DEFAULT_PLATFORM,
                    choices=sorted(PLATFORMS),
                    help="board this image is built for")
    args = ap.parse_args(argv)

    loader_path = os.path.join(args.build_dir, "boot", "BOOTARM.EFI")
    kernel_path = os.path.join(args.build_dir, "kernel", "KERNEL.ELF")
    if not os.path.exists(loader_path) or not os.path.exists(kernel_path):
        print("mkimage: build output missing; run make first", file=sys.stderr)
        return 1

    with open(loader_path, "rb") as f:
        loader = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    token = ("SRTOS-PLATFORM:%s" % args.platform).encode()
    if token not in kernel:
        print("mkimage: KERNEL.ELF was not built for %s.\n"
              "         rebuild it first:  py scripts/build.py kernel --platform %s"
              % (args.platform, args.platform), file=sys.stderr)
        return 1

    modules = {}
    svc_dir = os.path.join(args.build_dir, "services")
    if os.path.isdir(svc_dir):
        for name in sorted(os.listdir(svc_dir)):
            if name.upper().endswith(".SVC"):
                with open(os.path.join(svc_dir, name), "rb") as f:
                    modules[name.upper()] = f.read()

    os.makedirs(args.out, exist_ok=True)

    soc = PLATFORMS[args.platform]
    print("mkimage: %s (%s)" % (soc["board"], soc["soc"]))
    print("mkimage: ESP %d MiB" % (args.esp_size // MIB))
    esp = build_esp(args.build_dir, args.esp_size, loader, kernel, modules,
                    args.platform)
    if len(esp) > args.esp_size:
        print("mkimage: ESP does not fit", file=sys.stderr)
        return 1

    print("mkimage: install disk image %d MiB" %
          ((args.esp_size + args.data_size) // MIB))
    disk = build_disk(esp, args.data_size, args.esp_size)

    print("mkimage: USB image %d MiB" %
          ((args.esp_size + args.install_size) // MIB))
    usb = bytearray(args.esp_size + args.install_size)
    install_start = args.esp_size // 512
    install_fs = fat32.Fat32Builder(args.install_size, cluster_bytes=512,
                                    label="SRTOSINST")
    # the kernel mounts this partition at /install, so keep the names 8.3 clean
    install_fs.add_file("/SRTOS-DISK.IMG", disk)
    install_fs.add_file("/README.TXT", README_INSTALL.encode())
    install_img = install_fs.build()
    usb_sectors = len(usb) // 512
    gpt.build(usb, [
        dict(start=34, sectors=args.esp_size // 512 - 34, type=gpt.TYPE_ESP,
             name="EFI System Partition"),
        dict(start=install_start, sectors=usb_sectors - 33 - install_start,
             type=gpt.TYPE_LINUX_DATA, name="SRTOS-INSTALL"),
    ])
    mount_into(usb, 34, esp)
    mount_into(usb, install_start, install_img)

    outputs = {
        "BOOTARM.EFI": loader,
        "esp.img": esp,
        "srtos-disk.img": disk,
        "srtos-usb.img": bytes(usb),
    }
    manifest = ["SurfaceRT_OS image manifest", "built: %s" % time.ctime(),
                "platform: %s - %s (%s)" % (args.platform, soc["board"],
                                           soc["soc"]), ""]
    for name, data in outputs.items():
        path = os.path.join(args.out, name)
        with open(path, "wb") as f:
            f.write(data)
        digest = hashlib.sha256(data).hexdigest()
        manifest.append("%-18s %10d bytes  sha256 %s" % (name, len(data), digest))
        print("  %-18s %10d bytes" % (name, len(data)))

    if args.usb_package:
        pkg = emit_usb_package(args.out, args.platform,
                               os.path.join(args.out, "srtos-usb.img"))
        print("mkimage: USB package: %s" % pkg)

    parts = gpt.read(usb)
    manifest.append("")
    manifest.append("USB layout:")
    for p in parts:
        manifest.append("  %d: %-20s start LBA %-8d %d sectors" %
                        (p["index"], p["name"], p["start"], p["sectors"]))
    with open(os.path.join(args.out, "MANIFEST.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")

    print("\nmkimage: done. write the stick with:")
    print("  sudo dd if=%s/srtos-usb.img of=/dev/sdX bs=4M conv=fsync status=progress"
          % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
