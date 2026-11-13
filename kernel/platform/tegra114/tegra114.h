/*
 * NVIDIA Tegra 4 (T114) - the SoC in the Surface 2, a.k.a. "Surface RT 2"
 * (2013, second generation Windows RT tablet).
 *
 * Compared with the Tegra 3 port this SoC brings:
 *
 *   CPU    4x Cortex-A15 (+1 companion core) instead of 4x Cortex-A9
 *   L2     integrated in the A15 cluster, no external PL310
 *   Timer  ARM Generic Timer (CNTFRQ/CNTPCT/CNTP_TVAL) instead of the
 *          A9 private/global timers; the tick is the EL1 physical timer (PPI 30)
 *   EL     the firmware may hand off at EL2, so arch/arm/start.S drops to EL1
 *
 * Everything else the kernel touches (UART, I2C, GPIO, SDHCI, display, EHCI,
 * PMC, TMR) keeps the Tegra family addresses from tegra_common.h.
 *
 * The board wiring below is the same topology as the Surface RT (eMMC on
 * SDMMC4, microSD on SDMMC1, Wi-Fi on SDMMC2) and is marked as "to verify" in
 * docs/DRIVERS.md - it is the first thing to check on real hardware.
 */
#ifndef SRTOS_TEGRA114_H
#define SRTOS_TEGRA114_H

#define SRTOS_SOC_TEGRA114 1
#define SRTOS_SOC_NAME "Tegra 4 (T114)"
#define SRTOS_PLATFORM_TOKEN "tegra114"
#define SRTOS_BOARD_NAME "Surface 2 (2013)"

#include "../tegra/tegra_common.h"

/* ------------------------------------------------------------- CPU cores --- */
#define PLATFORM_CPU_CORES 4
#define PLATFORM_CPU_NAME "Cortex-A15"

/* ------------------------------------------------------------ interrupts --- */
/* ARM Generic Timer, non-secure EL1 physical timer (PPI 30). */
#define PLATFORM_IRQ_TIMER 30
#define PLATFORM_IRQ_UART_A 68
#define PLATFORM_TIMER_NAME "arm-generic"

/* --------------------------------------------------------- board wiring ---- */
#define PLATFORM_PMIC_I2C_BUS 4
#define PLATFORM_PMIC_I2C_ADDR 0x3C /* assumed: same MAX77663 family */
#define PLATFORM_SDMMC_EMMC 4
#define PLATFORM_SDMMC_SD 1
#define PLATFORM_SDMMC_WIFI 2
#define PLATFORM_WIFI_NAME "Marvell Avastar (T114 module)"
#define PLATFORM_PANEL_HINT "1920x1080"

#endif /* SRTOS_TEGRA114_H */
