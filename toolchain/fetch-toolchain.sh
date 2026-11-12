#!/bin/sh
# SurfaceRT_OS - toolchain bootstrap
#
# Downloads a *pinned* prebuilt GNU Arm Embedded toolchain into .toolchain/ so
# that a bare Linux/macOS box can build the OS without installing any SDK,
# package manager, or distro cross-toolchain.
#
#   ./toolchain/fetch-toolchain.sh            # install if missing
#   ./toolchain/fetch-toolchain.sh --force    # re-download
#
# If you already have arm-none-eabi-gcc on PATH, you do not need this script:
#   make CROSS_COMPILE=arm-none-eabi-
#
# Supported hosts: Linux x86_64, macOS (arm64/x86_64 - uses the xpack arm64 or
# x86_64 build), Windows via WSL (use the Linux x86_64 build).

set -eu

VERSION="13.2.1-1.1"
BASE="xpack-arm-none-eabi-gcc-${VERSION}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/.toolchain/${BASE}"
SHA256_LINUX_X64="1252a8cafe9237de27a765376697230368eec21db44dc3f1edeb8d838dabd530"

os="$(uname -s | tr 'A-Z' 'a-z')"
arch="$(uname -m)"

case "${os}-${arch}" in
  linux-x86_64|linux-amd64)  HOST="linux-x64"; SHA="${SHA256_LINUX_X64}" ;;
  linux-aarch64|linux-arm64) HOST="linux-arm64"; SHA="" ;;
  darwin-arm64)              HOST="darwin-arm64"; SHA="" ;;
  darwin-x86_64)             HOST="darwin-x64"; SHA="" ;;
  *)
    echo "error: unsupported host ${os}-${arch}." >&2
    echo "       Install 'gcc-arm-none-eabi' from your distro and run:" >&2
    echo "         make CROSS_COMPILE=arm-none-eabi-" >&2
    exit 1
    ;;
esac

if [ "${1:-}" != "--force" ] && [ -x "${DEST}/bin/arm-none-eabi-gcc" ]; then
  echo "toolchain already present: ${DEST}"
  exit 0
fi

TARBALL="${BASE}-${HOST}.tar.gz"
URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${VERSION}/${TARBALL}"

mkdir -p "${ROOT}/.toolchain"
tmp="${ROOT}/.toolchain/.download.$$"

echo "downloading $URL"
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 3 --progress-bar -o "${tmp}" "${URL}"
else
  wget -q --show-progress -O "${tmp}" "${URL}"
fi

if [ -n "${SHA}" ]; then
  got="$(sha256sum "${tmp}" | cut -d' ' -f1)"
  if [ "${got}" != "${SHA}" ]; then
    echo "error: checksum mismatch" >&2
    echo "  expected ${SHA}" >&2
    echo "  got      ${got}" >&2
    rm -f "${tmp}"
    exit 1
  fi
  echo "checksum ok"
fi

echo "extracting to ${ROOT}/.toolchain"
tar -xzf "${tmp}" -C "${ROOT}/.toolchain"
rm -f "${tmp}"

"${DEST}/bin/arm-none-eabi-gcc" --version | head -1
echo "done. now run: make"
