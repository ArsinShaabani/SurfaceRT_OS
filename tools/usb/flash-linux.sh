#!/bin/sh
# SurfaceRT_OS - write the USB image to a removable drive (Linux / macOS).
#
#   sudo ./tools/usb/flash-linux.sh dist/srtos-usb.img
#   sudo ./tools/usb/flash-linux.sh dist/tegra114/srtos-usb.img /dev/sdb
#
# Safety rules, in order: the device must be a block device, must not hold the
# running root file system, must have no mounted partitions, and the user has to
# type ERASE.  Afterwards the first 8 MiB are compared and verify_stick.py runs.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${here}/../.." && pwd)

image=${1:-}
if [ -z "${image}" ]; then
    for candidate in "${root}/dist/srtos-usb.img" \
                     "${root}/dist/tegra114/srtos-usb.img" \
                     "${root}/dist/tegra30/srtos-usb.img"; do
        if [ -f "${candidate}" ]; then image=${candidate}; break; fi
    done
fi
[ -n "${image}" ] || { echo "usage: $0 <image> [device]" >&2; exit 1; }
[ -f "${image}" ] || { echo "error: no such image: ${image}" >&2; exit 1; }

if [ "$(id -u)" -ne 0 ]; then
    echo "error: run me with sudo - raw disk access is needed" >&2
    exit 1
fi

image_size=$(wc -c < "${image}")
echo "image: ${image} ($((image_size / 1024 / 1024)) MiB)"
echo
echo "removable block devices:"
list=$(lsblk -dpno NAME,SIZE,TRAN,RM,MODEL 2>/dev/null | awk '$4==1 || tolower($3)=="usb"')
if [ -z "${list}" ]; then
    echo "  (none found - is the stick plugged in?)"
else
    echo "${list}" | sed 's/^/  /'
fi

dev=${2:-}
if [ -z "${dev}" ]; then
    printf '\ndevice to ERASE (for example /dev/sdb): '
    read -r dev
fi
[ -b "${dev}" ] || { echo "error: ${dev} is not a block device" >&2; exit 1; }

# never touch the disk that holds /
rootsrc=$(findmnt -n -o SOURCE / 2>/dev/null || echo "")
case "${rootsrc}" in
    "${dev}"*) echo "error: ${dev} holds the running system" >&2; exit 1 ;;
esac

if lsblk -nno MOUNTPOINT "${dev}" 2>/dev/null | grep -q .; then
    echo "error: ${dev} has mounted partitions - unmount them first:" >&2
    lsblk -nno NAME,MOUNTPOINT "${dev}" >&2
    exit 1
fi

dev_size=$(blockdev --getsize64 "${dev}")
if [ "${dev_size}" -lt "${image_size}" ]; then
    echo "error: ${dev} is smaller than the image" >&2
    exit 1
fi

printf '\nAbout to erase %s (%s) and write SurfaceRT_OS.\nType ERASE to continue: ' \
    "${dev}" "$(lsblk -dno SIZE "${dev}")"
read -r answer
[ "${answer}" = "ERASE" ] || { echo "aborted"; exit 1; }

echo
echo "writing..."
dd if="${image}" of="${dev}" bs=4M conv=fsync status=progress
sync

echo
echo "verifying the first 8 MiB..."
tmpa=$(mktemp) ; tmpb=$(mktemp)
trap 'rm -f "${tmpa}" "${tmpb}"' EXIT
dd if="${image}" of="${tmpa}" bs=1M count=8 status=none
dd if="${dev}" of="${tmpb}" bs=1M count=8 status=none
if ! cmp -s "${tmpa}" "${tmpb}"; then
    echo "error: read back differs from the image - the stick may be failing" >&2
    exit 1
fi
echo "read back identical"

python=${PYTHON:-}
if [ -z "${python}" ]; then
    for candidate in python3 python; do
        if command -v "${candidate}" >/dev/null 2>&1; then python=${candidate}; break; fi
    done
fi
if [ -n "${python}" ]; then
    "${python}" "${here}/verify_stick.py" "${dev}" || true
fi

echo
echo "Stick ready."
echo "  1. power the tablet off, insert the stick"
echo "  2. hold Volume-Down, press Power, release Volume-Down at the Surface logo"
echo "  3. bootable only if Secure Boot is off - see docs/UNLOCK-AND-BOOT.md"
