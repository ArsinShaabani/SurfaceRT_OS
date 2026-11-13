#!/usr/bin/env python3
"""Download and install the pinned ARM toolchain - on any host OS.

This is the single implementation behind all of these entry points:

    ./toolchain/fetch-toolchain.sh            Linux / macOS
    fetch-toolchain.cmd                       Windows (cmd.exe)
    powershell -File scripts\fetch-toolchain.ps1
    py scripts\fetch_toolchain.py             any OS
    py scripts\build.py all --fetch           build, fetching first if needed

Only the Python standard library is used (urllib, hashlib, tarfile, zipfile), so
it works on a stock Windows install without PowerShell modules, without admin
rights and without touching PATH.  The archive is verified against a pinned
SHA-256 before anything is unpacked.
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen

VERSION = "13.2.1-1.1"
BASE_NAME = "xpack-arm-none-eabi-gcc-%s" % VERSION
RELEASES = ("https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack"
            "/releases/download/v%s" % VERSION)

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN_ROOT = ROOT / ".toolchain"

# (os, arch) -> (asset suffix, sha256 or None to verify against the published .sha)
ASSETS = {
    ("windows", "x86_64"): ("win32-x64.zip",
                            "56b18ccb0a50f536332ec5de57799342ff0cd005ca2c54288c74759b51929e4f"),
    ("linux", "x86_64"): ("linux-x64.tar.gz",
                          "1252a8cafe9237de27a765376697230368eec21db44dc3f1edeb8d838dabd530"),
    ("linux", "aarch64"): ("linux-arm64.tar.gz", None),
    ("darwin", "arm64"): ("darwin-arm64.tar.gz", None),
    ("darwin", "x86_64"): ("darwin-x64.tar.gz", None),
}


def host_key():
    os_name = {"nt": "windows", "posix": "linux"}.get(os.name, os.name)
    if sys.platform.startswith("darwin"):
        os_name = "darwin"
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "x64": "x86_64", "arm64": "aarch64"}.get(
        machine, machine)
    return os_name, machine


def gcc_path(dest):
    exe = "arm-none-eabi-gcc.exe" if os.name == "nt" else "arm-none-eabi-gcc"
    return Path(dest) / "bin" / exe


def human(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return "%.1f %s" % (size, unit)
        size /= 1024.0


def download(url, target):
    print("downloading %s" % url)
    request = Request(url, headers={"User-Agent": "SurfaceRT_OS-build"})
    total = 0
    with urlopen(request) as response, open(target, "wb") as out:
        length = int(response.headers.get("Content-Length") or 0)
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
            total += len(chunk)
            if length and sys.stdout.isatty():
                sys.stdout.write("\r  %s / %s (%.0f%%)" %
                                 (human(total), human(length),
                                  100.0 * total / length))
                sys.stdout.flush()
    if length and sys.stdout.isatty():
        sys.stdout.write("\n")
    print("  %s downloaded" % human(total))
    return total


def published_sha(asset):
    """Fall back to the .sha file published next to the archive."""
    url = "%s/%s.sha" % (RELEASES, asset)
    print("fetching published checksum %s" % url)
    with urlopen(Request(url, headers={"User-Agent": "SurfaceRT_OS-build"})) as r:
        text = r.read().decode("ascii", "replace").strip()
    return text.split()[0].lower()


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract(archive, dest, suffix=None):
    """Unpack an archive.  `suffix` is the *asset* suffix, not the file name:
    downloads are staged as `.<asset>.part`, so the name cannot be trusted."""
    dest.mkdir(parents=True, exist_ok=True)
    kind = (suffix or archive.name).lower()
    if kind.endswith(".zip"):
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest)
    elif kind.endswith((".tar.gz", ".tgz")):
        with tarfile.open(archive, "r:gz") as tf:
            # Python 3.12+ warns about the default filter; be explicit.
            try:
                tf.extractall(dest, filter="data")
            except TypeError:
                tf.extractall(dest)
    else:
        raise SystemExit("unsupported archive type: %s" % archive.name)


def install(force=False, archive=None, quiet=False):
    key = host_key()
    if key not in ASSETS:
        raise SystemExit(
            "no prebuilt toolchain is published for %s/%s.\n"
            "Install gcc-arm-none-eabi for your OS and run the build with\n"
            "  --cross <prefix>   (for example --cross arm-none-eabi-)" % key)
    suffix, pinned = ASSETS[key]
    asset = "%s-%s" % (BASE_NAME, suffix)
    dest = TOOLCHAIN_ROOT / BASE_NAME
    gcc = gcc_path(dest)

    if gcc.exists() and not force:
        if not quiet:
            print("toolchain already present: %s" % dest)
        return 0

    TOOLCHAIN_ROOT.mkdir(parents=True, exist_ok=True)
    if archive:
        source = Path(archive)
        if not source.exists():
            raise SystemExit("archive not found: %s" % source)
        print("using local archive %s" % source)
    else:
        source = TOOLCHAIN_ROOT / (".%s.part" % asset)
        download("%s/%s" % (RELEASES, asset), source)

    expected = (pinned or published_sha(asset)).lower()
    actual = sha256_of(source)
    if actual != expected:
        if not archive:
            source.unlink(missing_ok=True)
        raise SystemExit("checksum mismatch\n  expected %s\n  got      %s" %
                         (expected, actual))
    print("checksum ok")

    if force and dest.exists():
        shutil.rmtree(dest)
    print("extracting to %s" % TOOLCHAIN_ROOT)
    try:
        extract(source, TOOLCHAIN_ROOT, suffix)
    except Exception as exc:
        if not archive:
            source.unlink(missing_ok=True)
        raise SystemExit("could not unpack %s: %s" % (asset, exc))
    if not archive:
        source.unlink(missing_ok=True)

    if not gcc.exists():
        raise SystemExit("extraction finished but %s is missing" % gcc)
    if os.name != "nt":
        for binary in (dest / "bin").iterdir():
            if binary.is_file():
                binary.chmod(binary.stat().st_mode | 0o111)
    try:
        version = subprocess.run([str(gcc), "--version"], capture_output=True,
                                 text=True).stdout.splitlines()[0]
    except (OSError, IndexError):
        version = "(could not run %s)" % gcc
    if not quiet:
        print(version)
        print("\ninstalled. now run:  %s scripts/build.py all" %
              ("py" if os.name == "nt" else "python3"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="install the pinned ARM toolchain")
    ap.add_argument("--force", action="store_true", help="reinstall even if present")
    ap.add_argument("--archive", help="use a local archive instead of downloading")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    return install(args.force, args.archive, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
