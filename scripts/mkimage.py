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
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fat32
import gpt
from platforms import PLATFORMS, DEFAULT_PLATFORM

MIB = 1024 * 1024

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


def main(argv=None):
    ap = argparse.ArgumentParser(description="build SurfaceRT_OS images")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--esp-size", type=int, default=40 * MIB)
    ap.add_argument("--data-size", type=int, default=96 * MIB)
    ap.add_argument("--install-size", type=int, default=224 * MIB)
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
