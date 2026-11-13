# Status: what runs, where, and what is still open

This project is deliberately explicit about the difference between "written,
boots and is tested" and "written against the TRM but not yet run on a real
tablet".  Nothing here is claimed to work on hardware until it has been run on
hardware.

Two boards are supported from the same tree - **Surface RT** (2012, Tegra 3 /
T30, Cortex-A9) and **Surface 2**, the tablet usually called "Surface RT 2"
(2013, Tegra 4 / T114, Cortex-A15).  `docs/SURFACE-MODELS.md` explains how to
tell them apart and exactly what differs in the kernel.

## Summary

| Area | State | Evidence |
|------|-------|----------|
| Build (loader, kernel, modules, images) | **works on Linux, macOS and Windows** | `make` / `build.cmd` on a clean checkout |
| Two targets: Surface RT (Tegra 3) and Surface 2 / "RT 2" (Tegra 4) | **both build**, per-platform kernels with mismatch protection | `make PLATFORM=tegra114`, `tests/pytests` platform test |
| Tegra 4 specifics (Cortex-A15, generic timer, EL2 drop, no PL310) | **implemented**, awaiting hardware | `docs/SURFACE-MODELS.md` |
| UEFI loader -> PE32/ARM conversion | **works** | `tools/elf2pe.py --selftest`, 198 relocations' worth of image verification |
| Kernel ELF relocation (load anywhere) | **works** | `tests/test_main.c` verifies every `R_ARM_ABS32` site |
| Host unit tests (heap, printf, strings) | **works** | `make test` |
| SRDP/1 receive engine | **works** | unit tests + live UDP run against the simulator |
| Wireless display sink (sender -> sink -> framebuffer) | **works on the host**, needs a Wi-Fi or USB netdev on the tablet | `sim/` + `host/srdisplay/sender.py` |
| Bootable USB image (GPT + FAT32 ESP + installer partition) | **works** | `dist/srtos-usb.img`, verified by the Python tests |
| Install to internal eMMC | **implemented**, needs a device | `installer_run()` + `install` shell command |
| Tegra 3 drivers (UART, GPIO, I2C, PMIC, SDHCI, DC, EHCI, SDIO) | **written against the TRM**, not yet validated on hardware | see `docs/DRIVERS.md` |
| Wi-Fi (Marvell 88W8797) | **SDIO enumeration + firmware upload path**; needs the vendor firmware blob, which cannot be redistributed | `kernel/drivers/net/wlan_88w8797.c` |
| Secure Boot on a stock Surface RT | **blocked by the device**, not by this project | `docs/BOOT-AND-SECUREBOOT.md` |

## What is verified by CI

`make test` runs two suites:

* `tests/test_main.c` (host, C): strings, printf, heap, SRDP raw/RLE/malformed
  input (including 2000 fuzzed packets), and a full relocation check of the
  built kernel image;
* `tests/pytests/run_tests.py`: ELF->PE conversion, FAT32 builder/reader round
  trip (including long file names), GPT writer/reader with CRC verification,
  SRDP wire-format vectors, and a complete image assembly run.

The display protocol has an end-to-end test that does not need a tablet:

```sh
make sim
build/sim/srtos-sim --width 320 --height 240 --frames 3 --out /tmp &
python3 host/srdisplay/sender.py --host 127.0.0.1 --source test \
        --width 320 --height 240 --frames 3
# -> srtos-sim: 3 frames, 6 rect blits, 0 dropped rects, 3 ppm written
```

The simulator links the exact `srdp_core.c` that ships in the kernel.

## Known gaps (ordered by what a real device would hit first)

1. **Secure Boot.** A stock Surface RT will refuse to run unsigned `BOOTARM.EFI`.
   See `docs/BOOT-AND-SECUREBOOT.md` for the options.
2. **Hardware validation of the drivers.** The register programming comes from
   the public Tegra 3 TRM, but the panel timing, SDHCI vendor clocks and the
   EHCI port reset sequence need a logic analyser or at least a serial log from
   a real device.
3. **Wi-Fi firmware.** The 88W8797 needs a vendor firmware image. The SDIO
   transport, CCCR probe and the upload path exist; without the blob the driver
   deliberately registers no interface instead of a broken one.
4. **Netdev for the tablet.** Until (3) is resolved the sink needs another
   transport. The stack is transport agnostic (`net_device`), so a USB Ethernet
   gadget or a USB Wi-Fi dongle is a contained addition (see
   `docs/EXTENDING.md`).
5. **No user space.** Services are kernel threads loaded from ELF modules with
   a versioned host API, not separate address spaces. This is a deliberate
   trade-off for a 1.3 GHz dual-issue core without an MMU-friendly TLB budget;
   `docs/EXTENDING.md` explains how isolation could be added.
