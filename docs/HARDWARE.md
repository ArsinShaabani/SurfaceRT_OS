# Hardware: Surface RT / NVIDIA Tegra 3 (T30)

Everything the kernel touches comes from the public Tegra 3 technical reference
manual.  All addresses live in `kernel/platform/tegra30/tegra30.h`; no driver
contains a hard coded address.

## SoC

| Block | Base | Notes |
|---|---|---|
| Cortex-A9 x4 (T30) | - | 1.3 GHz, 32 KiB/32 KiB L1, shared 1 MiB PL310 L2 |
| A9 private timer / global timer | `0x50040600` / `0x50040200` | tick and uptime |
| GIC-400 | `0x50041000` (dist), `0x50042000` (CPU) | PPI 27/29 = A9 timers |
| PL310 L2 | `0x50043000` | 1 MiB, 16 way |
| Tegra TMR (1 MHz counter) | `0x60005000` | timer calibration |
| GPIO | `0x6000D000` | 8 ports of 8 pins used by the kernel |
| Pin mux | `0x70000000` / `0x70000868` | |
| UART A..E | `0x70006000` + 0x40*n | UART-A is the debug console |
| I2C 1..5 | `0x7000C000` + 0x200*n | PMIC, fuel gauge, panel |
| PMC | `0x7000E400` | reset / power sequencing |
| Fuse | `0x7000F800` | chip ID, calibration |
| SDMMC 1..4 | `0x78000000` + 0x200*n | SDHCI compatible |
| Display controller 0/1 | `0x54200000` / `0x54240000` | panel / HDMI |
| DSI | `0x54300000` | |
| USB 1..3 | `0x7D000000` + 0x4000*n | EHCI host on USB1 |

RAM starts at `0x80000000`; everything below that is MMIO and is mapped as device
memory with execute-never.

## Surface RT specifics

| Thing | Value used here |
|---|---|
| Panel | 1366x768, 32 bpp, BGRX byte order, linear framebuffer from GOP |
| Internal eMMC | SDMMC4 -> block device `mmcblk0` |
| microSD slot | SDMMC1 -> `mmcblk1` |
| Wi-Fi | Marvell 88W8797 (Avastar) on SDMMC2, SDIO |
| PMIC | Maxim MAX77663 on I2C-4 at `0x3C` |
| Fuel gauge | MAX17047-class at `0x36` on I2C-4 |
| USB | micro-USB (device) + full size USB host via EHCI |
| Debug UART | UART-A, 115200 8N1 |

Board specific knowledge is confined to
`kernel/platform/tegra30/platform.c` (init order) and the slot table in
`kernel/drivers/block/sdmmc_tegra.c`.  Porting to another Tegra 3 board is a
sibling platform file plus a different slot table.

## Clocks

The loader passes `cpu_clock_hz = 1.3 GHz`, `uart_clock_hz = 2.6 MHz` and
`plt_clock_hz = 408 MHz` (the T30 APB clock) in `srt_bootinfo`.  The clock driver
does not trust them blindly: it measures the A9 timers against the Tegra 1 MHz
counter and only falls back to `cpu/2` if that counter is dead.

## Interrupt map used by the kernel

| IRQ | Source |
|---|---|
| 29 | Cortex-A9 private timer (scheduler tick) |
| 27 | Cortex-A9 global timer (unused as a source, kept free) |
| 68 | UART-A receive |

SPIs are routed to CPU 0 only; the kernel runs on one core (`SMP` is a documented
follow-up, see `docs/EXTENDING.md`).
