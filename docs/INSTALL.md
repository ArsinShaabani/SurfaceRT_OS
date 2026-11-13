# Booting from USB and installing to the internal eMMC

## 0. Which image should I build?

| Your tablet | Build command | Output |
|---|---|---|
| **Surface RT** (2012, Tegra 3, 1366x768) | `make` | `dist/srtos-usb.img` |
| **Surface 2**, a.k.a. **"Surface RT 2"** (2013, Tegra 4, 1920x1080) | `make PLATFORM=tegra114 OUT=dist/tegra114` | `dist/tegra114/srtos-usb.img` |
| Surface Pro (any) | - | not supported (x86, different firmware) |

The kernel and the image metadata must agree, and they cannot silently
disagree: the kernel embeds `SRTOS-PLATFORM:<id>`, `mkimage` refuses a mismatch,
and the ESP carries the id in `/SRTOS/PLATFORM`.  On the tablet, `uname` prints
the board it was built for.

## 1. Write the USB image

The build produces `dist/srtos-usb.img`, a GPT disk with two partitions:

| # | Name | Contents |
|---|---|---|
| 1 | `EFI System Partition` | FAT32: `\EFI\BOOT\BOOTARM.EFI`, `\EFI\Microsoft\Boot\bootmgfw.efi`, `\SRTOS\KERNEL.ELF`, `\SRTOS\BOOT.CFG`, `\SERVICES\HELLO.SVC` |
| 2 | `SRTOS-INSTALL` | FAT32: `SRTOS-DISK.IMG` (the image for the internal drive) and `README.TXT`; the kernel mounts this partition at `/install` |

```sh
lsblk                                  # find your stick, e.g. /dev/sdb
sudo dd if=dist/srtos-usb.img of=/dev/sdb bs=4M conv=fsync status=progress
sync
```

> The image is 264 MiB by default; the stick must be at least that large.
> Writing it destroys everything on the stick.

**Windows:** build the image with `build.cmd` (or `py scripts\build.py all`), then
write `dist\srtos-usb.img` with [Rufus](https://rufus.ie) in **DD image** mode
(or balenaEtcher).  Do not just copy the file to the stick - the stick must be a
raw GPT disk, not a FAT partition with a file on it.

**macOS:** `sudo dd if=dist/srtos-usb.img of=/dev/rdiskN bs=4m` (`diskutil list`
to find `N`; the raw `rdiskN` device is much faster).

## 2. Boot the tablet from the stick

* Power off.
* Hold **volume down** and press power; the firmware shows its boot menu.
* Pick the USB device.

If nothing happens, the device is refusing unsigned code - see
`docs/BOOT-AND-SECUREBOOT.md`.  That is a property of a locked Surface RT, not of
the image.

What you should see:

```
SurfaceRT_OS loader 1.0 (…)
target: Microsoft Surface RT / NVIDIA Tegra 3 (T30), ARMv7-A
loader: framebuffer 1366x768 stride 1408 @ 0x… (BGRX)
loader: kernel image 108204 bytes
loader: kernel placed at 0x82000000 (link 0x82000000) entry 0x820000e0, 0 relocations
loader: 27 memory regions, usable RAM 0x80000000-0x…
loader: exiting boot services
loader: jumping to the SurfaceRT_OS kernel

                    SurfaceRT_OS 1.0.0
     an ARMv7 kernel for the Microsoft Surface RT (Tegra 3)
…
srtos>
```

The same text goes to UART-A (115200 8N1) and to the panel.

`BOOT.CFG` on the ESP controls it:

```ini
kernel=KERNEL.ELF
kernel_base=0x82000000
cmdline=console=fb,uart loglevel=info
```

Add `loglevel=debug` for verbose driver output; the shell's `log 8` does the same
at run time.

## 3. Install to the internal eMMC

From the serial console on the tablet:

```
srtos> blk
  NAME      SIZE          BLOCK
  mmcblk0   29819 MiB     512
  …
srtos> install /install/srtos-disk.img 1
install: writing 136 MiB to mmcblk0 (278528 blocks) - do not power off
install: 12%
…
install: done, 136 MiB written and verified
```

The trailing `1` is the confirmation: without it the installer refuses to touch
the internal drive.  The installer writes the whole GPT image (partition table,
ESP, data partition) - the same bytes you tested from the stick.

After a power cycle the tablet boots from its own storage; the USB stick is no
longer needed.

### How much space does it use?

The default layout is 40 MiB ESP + 96 MiB data.  To use more of the drive:

```bash
make clean
python3 scripts/mkimage.py --data-size $((2*1024*1024*1024)) --install-size $((2400*1024*1024))
make image
```

Keep `--install-size` larger than ESP + data, because the USB stick has to carry
the disk image inside its second partition.

## 4. Updating

Replace `\SRTOS\KERNEL.ELF` on the ESP (from the tablet with `dispsink`-style
services stopped, or by writing the ESP from a PC).  The loader always boots
whatever kernel is on the partition, and the kernel header records the ABI it
expects from the loader (`SRT_BOOT_ABI`).

## 5. Recovering

The firmware still boots Windows RT if you keep its partition untouched; the
installer writes from LBA 0, so a device with Windows RT on it will lose it.
If you want to dual boot, use `--data-size` to leave room and do *not* let the
installer start at LBA 0 without editing `scripts/gpt.py` first - or simply keep
Windows RT on a different device.
