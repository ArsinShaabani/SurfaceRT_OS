#!/usr/bin/env python3
"""Python side of the SurfaceRT_OS test suite.

Covers the parts that are pure data plumbing: the ELF->PE converter, the FAT32
builder/reader pair, the GPT writer and the image assembly, plus a protocol
vector check against the C header sizes.
"""

import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "host", "srdisplay"))

import elf2pe
import fat32
import gpt
import srdp
from platforms import PLATFORMS, DEFAULT_PLATFORM

FAILURES = []
CHECKS = 0


def check(cond, msg):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILURES.append(msg)
        print("  FAIL %s" % msg)


def test_fat32():
    print("fat32 builder/reader")
    fs = fat32.Fat32Builder(40 * 1024 * 1024, cluster_bytes=512, label="TESTESP")
    fs.mkdir("/EFI/BOOT")
    fs.add_file("/EFI/BOOT/BOOTARM.EFI", b"MZ" + bytes(range(256)) * 40)
    fs.add_file("/EFI/MICROSOFT/BOOT/BOOTMGFW.EFI", b"MZ" + b"z" * 70000)
    fs.mkdir("/SRTOS")
    fs.add_file("/SRTOS/KERNEL.ELF", b"\x7fELF" + b"k" * 300)
    fs.add_file("/SRTOS/BOOT.CFG", b"kernel=KERNEL.ELF\n")
    fs.mkdir("/SERVICES")
    fs.add_file("/SERVICES/HELLO.SVC", b"module")
    img = fs.build()

    rd = fat32.Fat32Reader(img)
    check(rd.read("/EFI/BOOT/BOOTARM.EFI")[:2] == b"MZ", "BOOTARM.EFI signature")
    check(len(rd.read("/EFI/BOOT/BOOTARM.EFI")) == 10242, "BOOTARM.EFI size")
    check(len(rd.read("/EFI/MICROSOFT/BOOT/BOOTMGFW.EFI")) == 70002,
          "long name file size")
    check(rd.read("/SRTOS/BOOT.CFG") == b"kernel=KERNEL.ELF\n", "BOOT.CFG content")
    names = [n for n, d, s in rd.listdir("/EFI")]
    check("MICROSOFT" in names, "LFN directory name preserved")
    check(rd.read("/SERVICES/HELLO.SVC") == b"module", "module content")

    # the boot sector must describe a FAT32 volume the kernel can mount
    bs = img[0:512]
    check(struct.unpack_from("<H", bs, 11)[0] == 512, "bytes per sector")
    check(struct.unpack_from("<I", bs, 36)[0] > 0, "FAT size")
    check(struct.unpack_from("<I", bs, 44)[0] == 2, "root cluster")
    check(bs[82:90] == b"FAT32   ", "FAT32 type string")


def test_gpt():
    print("gpt writer/reader")
    img = bytearray(8 * 1024 * 1024)
    gpt.build(img, [
        dict(start=34, sectors=2048, type=gpt.TYPE_ESP, name="EFI System Partition"),
        dict(start=2082, sectors=4096, type=gpt.TYPE_LINUX_DATA, name="SRTOS-DATA"),
    ])
    parts = gpt.read(img)
    check(len(parts) == 2, "two partitions")
    check(parts[0]["start"] == 34, "esp start")
    check(parts[1]["start"] == 2082, "data start")
    check(parts[0]["name"] == "EFI System Partition", "esp name")
    check(img[450] == 0xEE, "protective MBR type")
    check(img[-512:-504] == b"EFI PART", "backup header present")
    # CRC of the primary header must be self consistent
    import zlib
    hdr = bytearray(img[512:512 + 92])
    crc = struct.unpack_from("<I", hdr, 16)[0]
    struct.pack_into("<I", hdr, 16, 0)
    check(zlib.crc32(bytes(hdr)) & 0xFFFFFFFF == crc, "header CRC")


def test_elf2pe():
    print("elf2pe")
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "elf2pe.py"),
                          "--selftest"], capture_output=True, text=True)
    check(out.returncode == 0, "elf2pe selftest: %s" % out.stdout.strip())
    check("relocs=2" in out.stdout, "elf2pe emitted relocations")


def test_protocol_vectors():
    print("srdp wire format")
    check(srdp.HDR.size == 24, "header is 24 bytes")
    check(srdp.RECT_BEGIN.size == 18, "rect_begin is 18 bytes")
    check(srdp.RECT_DATA.size == 8, "rect_data is 8 bytes")
    check(srdp.HELLO.size == 12, "hello is 12 bytes")
    check(srdp.HELLO_ACK.size == 12, "hello_ack is 12 bytes")
    pkt = srdp.hello(1366, 768, session=0x11223344)
    magic, ver, ptype, flags, session, seq, ts, plen = srdp.HDR.unpack_from(pkt, 0)
    check(magic == 0x50445253, "magic")
    check(ver == 1, "version")
    check(ptype == srdp.T_HELLO, "type")
    check(session == 0x11223344, "session echoed in seq")
    check(plen == 12, "payload length")
    w, h = struct.unpack_from("<HH", pkt, 24)
    check((w, h) == (1366, 768), "geometry")
    # fragmentation maths used by both sides
    check(srdp.fragments_for(1366 * 768 * 4) > 2000, "full frame fragment count")
    check(srdp.fragments_for(100) == 1, "small payload fits one datagram")
    # RLE round trip
    px = [0xFF112233] * 50 + [0xFF445566] * 30
    enc = srdp.rle_encode(px, len(px))
    check(enc is not None, "RLE used for flat runs")
    dec = []
    pos = 0
    while pos + 6 <= len(enc):
        count, value = struct.unpack_from("<HI", enc, pos)
        pos += 6
        dec.extend([value] * count)
    check(dec == px, "RLE round trip")


def test_toolchain_fetch_helpers():
    """The toolchain installer must unpack both published archive types.

    This guards the bug where a fresh install failed because the download is
    staged as `.<asset>.part` and the extractor sniffed the *file name*.
    """
    print("toolchain fetch helpers")
    import io
    import tarfile
    import tempfile
    import zipfile

    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import fetch_toolchain

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        inner = "xpack-arm-none-eabi-gcc-13.2.1-1.1/bin/arm-none-eabi-gcc"

        zip_path = tmp / ".asset.zip.part"
        with zipfile.ZipFile(zip_path, "w") as zf:
            zf.writestr(inner, "stub")
        fetch_toolchain.extract(zip_path, tmp / "zip", "win32-x64.zip")
        check((tmp / "zip" / inner).exists(), "zip asset extracted")

        tar_path = tmp / ".asset.tar.gz.part"
        with tarfile.open(tar_path, "w:gz") as tf:
            info = tarfile.TarInfo(inner)
            data = b"stub"
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
        fetch_toolchain.extract(tar_path, tmp / "tar", "linux-x64.tar.gz")
        check((tmp / "tar" / inner).exists(), "tarball asset extracted")

        # every platform asset must be a known archive type, and any pinned hash
        # must be a full SHA-256 (anything else falls back to the published .sha)
        for key, (suffix, pinned) in fetch_toolchain.ASSETS.items():
            label = "%s/%s" % key
            check(suffix.endswith((".zip", ".tar.gz")), "asset suffix %s" % label)
            check(pinned is None or (len(pinned) == 64 and
                                     all(c in "0123456789abcdef" for c in pinned)),
                  "pinned hash %s" % label)


def test_usb_package():
    """The one-command USB package must be complete and must verify itself."""
    print("usb package")
    kernel = os.path.join(ROOT, "build", "kernel", "KERNEL.ELF")
    if not os.path.exists(kernel):
        print("  SKIP: build the OS first (make)")
        return

    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        platform = DEFAULT_PLATFORM
        with open(kernel, "rb") as f:
            data = f.read()
        at = data.find(b"SRTOS-PLATFORM:")
        if at >= 0:
            platform = data[at + len(b"SRTOS-PLATFORM:"):].split(b"\x00")[0].decode()
        rc = subprocess.run(
            [sys.executable, os.path.join(ROOT, "scripts", "mkimage.py"),
             "--build-dir", os.path.join(ROOT, "build"), "--out", tmp,
             "--platform", platform, "--usb-package",
             "--esp-size", str(40 * 1024 * 1024),
             "--data-size", str(8 * 1024 * 1024),
             "--install-size", str(64 * 1024 * 1024)],
            capture_output=True, text=True)
        check(rc.returncode == 0, "mkimage --usb-package: %s" %
              (rc.stderr.strip()[-200:] or "ok"))

        pkg = os.path.join(tmp, "usb")
        for name in ("srtos-usb.img", "README.txt", "SHA256SUMS.txt",
                     "flash-windows.ps1", "flash-linux.sh", "device-check.ps1",
                     "install-esp.ps1", "verify_stick.py",
                     "UNLOCK-AND-BOOT.md", "scripts/fat32.py", "scripts/gpt.py"):
            check(os.path.exists(os.path.join(pkg, name)), "package has %s" % name)

        image = os.path.join(pkg, "srtos-usb.img")
        sums = open(os.path.join(pkg, "SHA256SUMS.txt")).read().split()
        digest = hashlib.sha256(open(image, "rb").read()).hexdigest()
        check(sums and sums[0] == digest, "SHA256SUMS matches the image")
        readme = open(os.path.join(pkg, "README.txt")).read()
        check(platform in readme, "README names the platform")
        check("Volume-Down" in readme, "README explains the boot buttons")
        guide = open(os.path.join(pkg, "UNLOCK-AND-BOOT.md")).read()
        check("Golden Keys" in guide and "Yahallo" in guide,
              "unlock guide covers both stages")

        # the packaged verifier must work standalone (no repository around it)
        rc = subprocess.run([sys.executable, os.path.join(pkg, "verify_stick.py"),
                             image], capture_output=True, text=True)
        check(rc.returncode == 0, "packaged verifier accepts the image: %s" %
              (rc.stdout.strip().splitlines() or [""])[-1])

        # ... and must reject a damaged one
        broken = os.path.join(tmp, "broken.img")
        with open(image, "rb") as src, open(broken, "wb") as dst:
            dst.write(src.read())
        with open(broken, "r+b") as f:
            f.seek(0)
            f.write(b"\x00" * 512)
        rc = subprocess.run([sys.executable, os.path.join(pkg, "verify_stick.py"),
                             broken], capture_output=True, text=True)
        check(rc.returncode != 0, "packaged verifier rejects a wiped MBR")


def test_platforms():
    """Both supported boards must be described consistently end to end."""
    print("platforms")
    expected = {"tegra30": "cortex-a9", "tegra114": "cortex-a15"}
    check(set(PLATFORMS) == set(expected), "platform table entries")
    for name, mcpu in expected.items():
        soc = PLATFORMS[name]
        check(soc["mcpu"] == mcpu, "%s cpu" % name)
        header = os.path.join(ROOT, "kernel", "platform", name, name + ".h")
        check(os.path.exists(header), "%s SoC header" % name)
        if os.path.exists(header):
            text = open(header).read()
            check('SRTOS_PLATFORM_TOKEN "%s"' % name in text,
                  "%s platform token" % name)
            check(soc["define"] in text, "%s SoC define" % name)
    # the family wide register map must exist and be shared
    check(os.path.exists(os.path.join(ROOT, "kernel", "platform", "tegra",
                                      "tegra_common.h")),
          "shared tegra register map")

    # build.py must expose both platforms and pick the right -mcpu
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import build
    for name, mcpu in expected.items():
        flags = build.kernel_flags(name)
        check("-mcpu=%s" % mcpu in flags, "%s compile flags" % name)
        check("-D%s=1" % PLATFORMS[name]["define"] in flags,
              "%s soc define flag" % name)
    check(build.kernel_flags("tegra114") != build.kernel_flags("tegra30"),
          "per-platform flags differ")

    # mkimage must refuse a kernel built for the other board
    kernel = os.path.join(ROOT, "build", "kernel", "KERNEL.ELF")
    if os.path.exists(kernel):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            data = open(kernel, "rb").read()
            marker = b"SRTOS-PLATFORM:"
            at = data.find(marker)
            built = data[at + len(marker):].split(b"\x00")[0].decode()                 if at >= 0 else "tegra30"
            other = [p for p in PLATFORMS if p != built][0]
            rc = subprocess.run(
                [sys.executable, os.path.join(ROOT, "scripts", "mkimage.py"),
                 "--build-dir", os.path.join(ROOT, "build"), "--out", tmp,
                 "--platform", other],
                capture_output=True, text=True)
            check(rc.returncode != 0, "mkimage rejects a kernel for another board")
            check("not built for" in (rc.stderr + rc.stdout),
                  "mkimage explains the mismatch")


def test_images():
    print("image assembly")
    build = os.path.join(ROOT, "build")
    loader = os.path.join(build, "boot", "BOOTARM.EFI")
    kernel = os.path.join(build, "kernel", "KERNEL.ELF")
    if not os.path.exists(loader) or not os.path.exists(kernel):
        print("  SKIP: build the OS first (make)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        with open(kernel, "rb") as f:
            kernel_bytes = f.read()
        marker = b"SRTOS-PLATFORM:"
        at = kernel_bytes.find(marker)
        platform = DEFAULT_PLATFORM
        if at >= 0:
            platform = kernel_bytes[at + len(marker):].split(b"\x00")[0].decode()
        rc = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkimage.py"),
                             "--build-dir", build, "--out", tmp,
                             "--platform", platform,
                             "--esp-size", str(40 * 1024 * 1024),
                             "--data-size", str(8 * 1024 * 1024),
                             "--install-size", str(64 * 1024 * 1024)],
                            capture_output=True, text=True)
        check(rc.returncode == 0, "mkimage: %s" % rc.stderr.strip()[-200:])
        for name in ("esp.img", "srtos-disk.img", "srtos-usb.img", "MANIFEST.txt"):
            check(os.path.exists(os.path.join(tmp, name)), "output %s" % name)
        with open(os.path.join(tmp, "esp.img"), "rb") as f:
            esp = f.read()
        rd = fat32.Fat32Reader(esp)
        efi = rd.read("/EFI/BOOT/BOOTARM.EFI")
        info = elf2pe.parse_pe(efi)
        check(info["machine"] == 0x01C4, "loader machine is ARMNT")
        check(info["subsystem"] == 10, "loader subsystem is EFI application")
        check(info["sections"][0]["name"] == ".text", "loader starts with .text")
        check(efi[:2] == b"MZ", "loader has a DOS header")
        check(os.path.exists(os.path.join(tmp, "MANIFEST.txt")), "manifest")
        kernel2 = rd.read("/SRTOS/KERNEL.ELF")
        check(kernel2[:4] == b"\x7fELF", "kernel is an ELF")
        check(kernel2[18:20] == b"\x28\x00", "kernel machine is ARM (0x28)")
        check(rd.read("/SRTOS/PLATFORM").decode().startswith(platform),
              "platform marker on the ESP")
        check(rd.read("/SRTOS/DEFAULT")[:5] == b"srtos", "boot selector on the ESP")
        cfg = rd.read("/SRTOS/BOOT.CFG")
        check(b"kernel=KERNEL.ELF" in cfg, "boot config content")
        with open(os.path.join(tmp, "srtos-usb.img"), "rb") as f:
            f.seek(0)
            head = f.read(1024)
        check(head[450] == 0xEE, "USB image has a protective MBR")


def main():
    print("SurfaceRT_OS python tests\n")
    test_elf2pe()
    test_fat32()
    test_gpt()
    test_platforms()
    test_usb_package()
    test_protocol_vectors()
    test_toolchain_fetch_helpers()
    test_images()
    print("\n%d checks, %d failure(s)" % (CHECKS, len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
