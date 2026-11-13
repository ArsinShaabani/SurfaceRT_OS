#!/bin/sh
# SurfaceRT_OS - toolchain bootstrap (Linux / macOS)
#
#   ./toolchain/fetch-toolchain.sh            install if missing
#   ./toolchain/fetch-toolchain.sh --force    re-download
#   ./toolchain/fetch-toolchain.sh --archive gcc.tar.gz   offline install
#
# The actual work (pinned version, SHA-256 verification, extraction) lives in
# scripts/fetch_toolchain.py so that Windows and POSIX install the same thing.
#
# Already have arm-none-eabi-gcc? You do not need this file:
#   make CROSS_COMPILE=arm-none-eabi-

set -eu
here=$(cd "$(dirname "$0")" && pwd)

if command -v python3 >/dev/null 2>&1; then
    py=python3
elif command -v python >/dev/null 2>&1; then
    py=python
else
    echo "error: Python 3 is required (https://python.org)" >&2
    exit 1
fi

exec "$py" "$here/../scripts/fetch_toolchain.py" "$@"
