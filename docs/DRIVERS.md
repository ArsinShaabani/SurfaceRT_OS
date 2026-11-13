# Driver status

"Implemented" means the register programming and the kernel integration are
written and compile; "validated" means it has been run on a real Surface RT and
survived.  Be honest with yourself about the difference before flashing a device.

| Driver | File | State | Notes |
|---|---|---|---|
| UART-A console + RX interrupt | `drivers/serial/uart_tegra.c` | implemented | 115200 8N1, 256 byte RX ring, shell input |
| GPIO | `drivers/gpio/gpio_tegra.c` | implemented | CNF/OE/OUT/IN, pin ownership table |
| Pin mux | `platform/tegra30/tegra30.h` | partially | only the pins the drivers need |
| I2C (polled master) | `drivers/i2c/i2c_tegra.c` | implemented | CMD1 + repeated start read, timeouts on every transaction |
| MAX77663 PMIC | `drivers/pmic/max77663.c` | implemented | power off, reboot, RTC, presence probe |
| Fuel gauge | same file | implemented | single SOC register read |
| SDHCI host (eMMC/microSD) | `drivers/block/sdmmc_tegra.c` | implemented | reset, clock, SD + MMC identification, CSD v2, PIO multi-block |
| SDIO transport | `drivers/block/sdmmc_tegra.c` | implemented | CMD52/CMD53 for the Wi-Fi module |
| Display controller | `drivers/video/dc_tegra.c` | implemented | window A programming + vblank flip; panel timing is left as the firmware set it |
| Framebuffer + console | `drivers/video/fb.c` | implemented | 5x7 font, scrolling console, format converting blitter |
| EHCI host + HID keyboard | `drivers/usb/ehci_tegra.c` | implemented | controller reset, port reset, control transfers via qTD chains, boot-protocol polling |
| Marvell 88W8797 Wi-Fi | `drivers/net/wlan_88w8797.c` | transport done, firmware required | needs `/firmware/88w8797.bin`; no interface is registered without it |
| Thermal / fuse | `drivers/misc/tegra_misc.c` | partial | chip ID + revision; temperature needs the per-SKU calibration table |
| Input core | `drivers/input/input.c` | implemented | queue, keymap, shell injection |

## What "not yet validated" means in practice

Three things are most likely to need adjustment on a real device, in this order:

1. **SDHCI vendor clocks.** Tegra 3 needs its vendor clock control
   (`0x100`) programmed for high speed modes. The driver runs at 25 MHz after
   identification, which works for the boot partitions but not at full eMMC
   speed. Add `SDHCI_VENDOR_CLOCK_CTRL` programming once measured.
2. **Panel / DC hand-off.** The driver keeps the firmware's timing and only
   reprograms the window, which is the least invasive option. If the panel goes
   blank, revert to the firmware framebuffer (`dc_init()` returning -1 keeps it).
3. **EHCI reset timing.** The port reset delays (50 ms + 20 ms) are the USB 2.0
   maxima; some hubs need the port status polled rather than delayed.

Each of these is a small, localised change - which is why the drivers are split
by block and the register map is centralised.

## Adding a driver

1. Add the register block to `kernel/platform/tegra30/tegra30.h`.
2. Write the driver under `kernel/drivers/<class>/` with an `xxx_init()` entry.
3. Call it from `platform_init()` in the right order.
4. Register the device with the subsystem (`block_register`, `net_register_device`,
   `driver_register`, ...).

No build changes are needed: `scripts/build.py` globs `kernel/drivers/*/*.c`.
That is also why the tree builds identically on Windows and Linux - there is
no per-file list to keep in sync.
