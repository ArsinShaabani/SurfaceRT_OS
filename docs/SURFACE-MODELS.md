# Which Surface is which (and what the kernel does about it)

Microsoft's naming is the single biggest source of confusion around this
project, so here is the table the code follows:

| Marketing name | Year | SoC | CPU | RAM | Panel | Platform id |
|---|---|---|---|---|---|---|
| **Surface RT** ("Surface RT 1", first generation) | 2012 | NVIDIA **Tegra 3** (T30) | 4x **Cortex-A9** @ 1.3 GHz | 2 GB | 1366x768 | `tegra30` |
| **Surface 2** (sold as the successor of the Surface RT, often called "Surface RT 2") | 2013 | NVIDIA **Tegra 4** (T114) | 4x **Cortex-A15** @ 1.8 GHz (+ companion core) | 2 GB | **1920x1080** | `tegra114` |
| Surface Pro / Pro 2 / ... | 2013+ | Intel x86 | - | - | - | **not supported** (different firmware, different boot chain) |

So: if your device is the *original* Surface RT with the kickstand and the
1366x768 screen, it is Tegra 3.  If it is the thinner silver one with the
1920x1080 screen and the two-position kickstand, it is the **Surface 2** -
the Tegra 4 machine.  Both are supported by this tree; the Surface Pro family
is not, and cannot be: it is a PC, not an ARM UEFI tablet.

## What actually differs in the kernel

| Concern | `tegra30` (Surface RT) | `tegra114` (Surface 2) |
|---|---|---|
| Compiler target | `-mcpu=cortex-a9` | `-mcpu=cortex-a15` |
| Timekeeping counter | Cortex-A9 global timer (`0x50040200`) | ARM generic timer `CNTPCT` (`mrrc p15, 0, .., c14`) |
| Scheduler tick | A9 private timer, PPI **29** | EL1 physical timer `CNTP_TVAL`, PPI **30** |
| L2 cache | external PL310 at `0x50043000` | inside the A15 cluster, nothing to program |
| Boot exception level | SVC (EL1) from the firmware | may be **EL2**: `start.S` drops to EL1 and clears `HCR.VM/IMO/FMO/AMO`, sets `CNTHCTL.EL1PCEN` |
| Watchdog fallback on reboot | A9 per-CPU watchdog | none (PMC master reset only, as documented) |
| Everything else | identical: UART, I2C, GPIO, SDHCI, display, EHCI, PMC, fuse, TMR |

The register map for the shared blocks lives in `kernel/platform/tegra/tegra_common.h`;
the per-SoC facts live in `kernel/platform/tegra30/tegra30.h` and
`kernel/platform/tegra114/tegra114.h`.  Drivers only ever include
`<srtos/soc.h>`.

## The loader is shared

`BOOTARM.EFI` is identical for both boards: it only uses UEFI services (GOP,
SimpleFileSystem, `ExitBootServices`) and is compiled for the ARMv7-A baseline,
so it runs on a Cortex-A9 and on a Cortex-A15 unchanged.  Only the kernel is
board specific - which is why the mismatch check lives in the image builder and
not in the loader.

## Building for each one

```sh
# Surface RT (Tegra 3) - the default
make

# Surface 2 / "Surface RT 2" (Tegra 4)
make PLATFORM=tegra114 OUT=dist/tegra114
```

On Windows:

```bat
py scripts\build.py all
py scripts\build.py all --platform tegra114 --out dist\tegra114
```

Two short commands that show what a build is for:

```sh
py scripts/build.py doctor                     # lists both platforms
py scripts/build.py kernel --platform tegra114 # kernel only
```

**Mismatch protection**: the kernel embeds a `SRTOS-PLATFORM:<id>` string and
`scripts/mkimage.py` refuses to build an ESP around a kernel for the other
board, so you cannot end up with a Tegra 3 kernel on a Surface 2 stick by
accident.  The id lands on the ESP as `/SRTOS/PLATFORM` and in `BOOT.CFG`, and
the shell prints it:

```
srtos> uname
SurfaceRT_OS 1.0.0 (Tegra Sink)
board: Surface 2 (2013), NVIDIA Tegra 4 (T114), 4 x Cortex-A15 (1 core online)
```

## What is verified, and what is not

Both platforms **build** from the same tree and the build is regression tested
(kernel image relocation, image assembly, platform marker, mismatch refusal).
Neither has been run on a real tablet: that is what `docs/STATUS.md` and
`docs/DRIVERS.md` keep repeating, and the Tegra 4 port is the one that needs
hardware eyes most, because:

1. the panel timing is still whatever the firmware programmed (the kernel only
   takes over the window), so 1920x1080 works through the firmware
   framebuffer even before the display controller driver is validated;
2. the board wiring in `tegra114.h` (eMMC on SDMMC4, microSD on SDMMC1, Wi-Fi on
   SDMMC2, MAX77663 at I2C-4/0x3C) is the Surface RT topology carried over -
   `blk`, `i2c` and `drivers` in the shell will confirm or refute it in
   seconds;
3. Secure Boot is locked on the Surface 2 as well, for the same reasons as the
   Surface RT (`docs/BOOT-AND-SECUREBOOT.md`).
