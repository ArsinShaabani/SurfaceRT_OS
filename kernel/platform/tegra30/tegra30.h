/*
 * NVIDIA Tegra 3 (T30) - the SoC in the Surface RT (2012, first generation).
 *
 * Shared blocks (UART, I2C, GPIO, SDHCI, display, USB, ...) come from
 * tegra_common.h; this file adds what makes T30 a T30: four Cortex-A9 cores,
 * the A9 private/global timers, and the external PL310 L2 controller.
 */
#ifndef SRTOS_TEGRA30_H
#define SRTOS_TEGRA30_H

#define SRTOS_SOC_TEGRA30 1
#define SRTOS_SOC_NAME "Tegra 3 (T30)"
#define SRTOS_PLATFORM_TOKEN "tegra30"
#define SRTOS_BOARD_NAME "Surface RT (2012)"

#include "../tegra/tegra_common.h"

/* ------------------------------------------------------------- CPU cores --- */
#define PLATFORM_CPU_CORES 4
#define PLATFORM_CPU_NAME "Cortex-A9"

/* ------------------------------------------------------- A9 private block --- */
#define TEGRA_A9_PERIPH_BASE 0x50040000u
#define TEGRA_A9_PRIVATE_TIMER (TEGRA_A9_PERIPH_BASE + 0x600)
#define TEGRA_A9_WATCHDOG (TEGRA_A9_PERIPH_BASE + 0x620)
#define TEGRA_A9_GLOBAL_TIMER (TEGRA_A9_PERIPH_BASE + 0x200)
#define TEGRA_L2C_BASE 0x50043000u /* external PL310 */

/* A9 private timer / global timer registers */
#define A9_TMR_LOAD 0x00
#define A9_TMR_COUNTER 0x04
#define A9_TMR_CONTROL 0x08
#define A9_TMR_ISR 0x0C
#define A9_TMR_CTRL_ENABLE BIT(0)
#define A9_TMR_CTRL_AUTORELOAD BIT(1)
#define A9_TMR_CTRL_IRQEN BIT(2)
#define A9_TMR_CTRL_PRESCALE_SHIFT 8

#define A9_GTMR_COUNTER_LO 0x00
#define A9_GTMR_COUNTER_HI 0x04
#define A9_GTMR_CONTROL 0x08
#define A9_GTMR_CTRL_ENABLE BIT(0)
#define A9_GTMR_CTRL_AUTONC BIT(3)
#define A9_GTMR_CTRL_PRESCALE_SHIFT 8

/* PL310 / L2C-310 */
#define L2C_CTRL 0x100
#define L2C_AUX_CTRL 0x104
#define L2C_TAG_CTRL 0x108
#define L2C_INV_WAY 0x77C
#define L2C_CLEAN_INV_WAY 0x7FC
#define L2C_CTRL_ENABLE BIT(0)

/* ------------------------------------------------------------ interrupts --- */
#define PLATFORM_IRQ_TIMER 29 /* Cortex-A9 private timer, PPI 29 */
#define PLATFORM_IRQ_UART_A 68 /* SPI 36 */
#define PLATFORM_TIMER_NAME "a9-private"

/* --------------------------------------------------------- board wiring ---- */
#define PLATFORM_PMIC_I2C_BUS 4
#define PLATFORM_PMIC_I2C_ADDR 0x3C /* MAX77663 */
#define PLATFORM_SDMMC_EMMC 4      /* internal eMMC   -> mmcblk0 */
#define PLATFORM_SDMMC_SD 1        /* microSD slot    -> mmcblk1 */
#define PLATFORM_SDMMC_WIFI 2      /* Marvell 88W8797 (SDIO)      */
#define PLATFORM_WIFI_NAME "Marvell 88W8797"
#define PLATFORM_PANEL_HINT "1366x768"

#endif /* SRTOS_TEGRA30_H */
