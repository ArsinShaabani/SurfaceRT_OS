/*
 * NVIDIA Tegra 3 (T30) register map - the SoC inside the Surface RT.
 *
 * All addresses are physical and come from the public Tegra 3 TRM chapter on
 * the memory map.  Every block the kernel touches is listed here so that a
 * board variant (Surface RT vs. a Tegra 3 dev board) only needs a new
 * platform file, not driver changes.
 *
 * NOTE: nothing outside this file may contain a hard coded address.
 */
#ifndef SRTOS_TEGRA30_H
#define SRTOS_TEGRA30_H

#include <srtos/types.h>

/* ---------------------------------------------------------------- memory --- */
#define T30_RAM_BASE 0x80000000u
#define T30_RAM_SIZE_MAX 0x80000000u /* up to 2 GiB */

/* ----------------------------------------------------------- ARM periph. --- */
#define T30_A9_PERIPH_BASE 0x50040000u
#define T30_A9_PRIVATE_TIMER (T30_A9_PERIPH_BASE + 0x600)
#define T30_A9_WATCHDOG (T30_A9_PERIPH_BASE + 0x620)
#define T30_A9_GLOBAL_TIMER (T30_A9_PERIPH_BASE + 0x200)
#define T30_GIC_DIST_BASE 0x50041000u
#define T30_GIC_CPU_BASE 0x50042000u
#define T30_L2C_BASE 0x50043000u

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

/* L2C-310 (PL310) */
#define L2C_CTRL 0x100
#define L2C_AUX_CTRL 0x104
#define L2C_TAG_CTRL 0x108
#define L2C_INV_WAY 0x77C
#define L2C_CLEAN_INV_WAY 0x7FC
#define L2C_CTRL_ENABLE BIT(0)

/* -------------------------------------------------------------- APB misc --- */
#define T30_TMR_BASE 0x60005000u
#define TMR_USEC_CNTR 0x00
#define TMR_USEC_CFG 0x0C
#define TMR_FREEZE 0x14
#define T30_TMR_TIMER_BASE 0x60005008u /* TIMER1..TIMER10, 8 bytes each */
#define TMR_PTV 0x00
#define TMR_PCR 0x04
#define TMR_PCR_INTR BIT(30)
#define TMR_PTV_EN BIT(31)
#define TMR_PTV_PERIODIC BIT(30)

#define T30_GPIO_BASE 0x6000D000u
#define T30_EXTERNAL_MEMORY_BASE 0x6000C000u
#define T30_MC_BASE 0x7000F000u
#define T30_EMC_BASE 0x7000F400u

/* GPIO ports A..Z (7 ports: A..GG on T30: 8 groups of 32) */
#define T30_GPIO_PORTS 8
#define GPIO_CNF 0x00
#define GPIO_OE 0x10
#define GPIO_OUT 0x20
#define GPIO_IN 0x30
#define GPIO_INT_STA 0x40
#define GPIO_INT_ENB 0x50
#define GPIO_INT_LVL 0x60
#define GPIO_INT_CLR 0x70

/* -------------------------------------------------------------- pin mux --- */
#define T30_PINMUX_BASE 0x70000868u
#define T30_PINGROUP_BASE 0x70000000u

/* ------------------------------------------------------------------ UART --- */
#define T30_UART_BASE(n) (0x70006000u + (n) * 0x40u)
#define UART_THR 0x00
#define UART_RBR 0x00
#define UART_IER 0x04
#define UART_IIR 0x08
#define UART_FCR 0x08
#define UART_LCR 0x0C
#define UART_MCR 0x10
#define UART_LSR 0x14
#define UART_MSR 0x18
#define UART_SPR 0x1C
#define UART_IRDA_CSR 0x20
#define UART_RX_FIFO_CFG 0x24
#define UART_MIE 0x28
#define UART_ASR 0x2C

#define UART_LSR_DR 0x01
#define UART_LSR_OE 0x02
#define UART_LSR_THRE 0x20
#define UART_LSR_TEMT 0x40
#define UART_LCR_DLAB 0x80
#define UART_LCR_8N1 0x03
#define UART_FCR_EN 0x01
#define UART_FCR_CLR_RX 0x02
#define UART_FCR_CLR_TX 0x04
#define UART_IER_RDA 0x01
#define UART_IER_THRE 0x02

/* ------------------------------------------------------------------- I2C --- */
#define T30_I2C_BASE(n) (0x7000C000u + (n) * 0x200u)
#define I2C_CNFG 0x00
#define I2C_CMD_ADDR0 0x04
#define I2C_CMD_DATA1 0x08
#define I2C_CMD_DATA2 0x0C
#define I2C_STATUS 0x1C
#define I2C_SLCG 0x20
#define I2C_INTR_MASK 0x3C
#define I2C_INTR_STATUS 0x40
#define I2C_CLK_DIV 0x6C
#define I2C_CNFG_NEW_MASTER BIT(11)
#define I2C_CNFG_START BIT(5)
#define I2C_CNFG_CMD1 BIT(4)
#define I2C_CNFG_DEBOUNCE 0x02
#define I2C_STATUS_BUSY BIT(0)
#define I2C_STATUS_RCVD BIT(2)
#define I2C_STATUS_NOACK BIT(3)
#define I2C_STATUS_CMD1_STAT_SHIFT 4
#define I2C_CMD_READ 0x01
#define I2C_CMD_WRITE 0x00

/* ------------------------------------------------------------------ PMC ---- */
#define T30_PMC_BASE 0x7000E400u
#define PMC_CNTRL 0x00
#define PMC_CNTRL_MAIN_RST BIT(4)
#define PMC_CNTRL_PWRREQ_OE BIT(10)
#define PMC_SCRATCH0 0x50
#define PMC_SCRATCH1 0x54
#define PMC_SCRATCH39 0x138
#define PMC_RST_STATUS 0x1B4
#define PMC_DPD_PADS_ORIDE 0x1C
#define PMC_PWRGATE_TOGGLE 0x30
#define PMC_PWRGATE_STATUS 0x38
#define PMC_DEVICE_RSVDATA 0x714
#define PMC_POR_IO_PWRDN_ALLOW 0x7DC

/* ---------------------------------------------------------------- fuses --- */
#define T30_FUSE_BASE 0x7000F800u
#define FUSE_SKU_DIRECT_CONFIG 0x1F8
#define FUSE_SPARE_BIT 0x250

/* --------------------------------------------------------------- SDMMC ---- */
#define T30_SDMMC_BASE(n) (0x78000000u + (n) * 0x200u)
#define SDHCI_DMA_ADDRESS 0x00
#define SDHCI_BLOCK_SIZE 0x04
#define SDHCI_BLOCK_COUNT 0x06
#define SDHCI_ARGUMENT 0x08
#define SDHCI_TRANSFER_MODE 0x0C
#define SDHCI_COMMAND 0x0E
#define SDHCI_RESPONSE 0x10
#define SDHCI_BUFFER 0x20
#define SDHCI_PRESENT_STATE 0x24
#define SDHCI_HOST_CONTROL 0x28
#define SDHCI_POWER_CONTROL 0x29
#define SDHCI_CLOCK_CONTROL 0x2C
#define SDHCI_TIMEOUT_CONTROL 0x2E
#define SDHCI_SOFTWARE_RESET 0x2F
#define SDHCI_NORMAL_INT_STATUS 0x30
#define SDHCI_ERROR_INT_STATUS 0x32
#define SDHCI_NORMAL_INT_ENABLE 0x34
#define SDHCI_ERROR_INT_ENABLE 0x36
#define SDHCI_NORMAL_INT_SIGNAL 0x38
#define SDHCI_ERROR_INT_SIGNAL 0x3A
#define SDHCI_CAPABILITIES 0x40
#define SDHCI_HOST_VERSION 0xFE
#define SDHCI_VENDOR_CLOCK_CTRL 0x100 /* Tegra specific */
#define SDHCI_VENDOR_CAP_OVERRIDES 0x108
#define SDHCI_VENDOR_MISC_CTRL 0x120
#define SDHCI_SPARE_CSR 0x160
#define SDHCI_VENDOR_TUNING_CTRL 0x1C0

/* ------------------------------------------------------------------- DC ---- */
#define T30_DC_BASE(n) ((n) ? 0x54240000u : 0x54200000u)
#define DC_CMD_STATE_ACCESS 0x000
#define DC_CMD_STATE_CONTROL 0x004
#define DC_DISP_DISP_SIGNAL_OPTIONS0 0x400
#define DC_DISP_DISP_WIN_OPTIONS 0x404
#define DC_DISP_DISP_MEM_HIGH_PRIORITY 0x406
#define DC_DISP_DISP_CLOCK_CONTROL 0x408
#define DC_DISP_DISP_INTERFACE_CONTROL 0x40A
#define DC_DISP_DISP_COLOR_CONTROL 0x40C
#define DC_DISP_SHIFT_CLOCK_OPTIONS 0x410
#define DC_DISP_DATA_ENABLE_OPTIONS 0x412
#define DC_DISP_SERIAL_INTERFACE_OPTIONS 0x414
#define DC_DISP_LCD_SPI_OPTIONS 0x416
#define DC_DISP_BORDER_COLOR 0x418
#define DC_DISP_COLOR_KEY0_LOW 0x41C
#define DC_DISP_COLOR_KEY0_HIGH 0x420
#define DC_DISP_DC_MCCIF_FIFOCTRL 0x480
#define DC_DISP_SD_SELECT 0x4B0
#define DC_DISP_BLEND_BACKGROUND_COLOR 0x4BC
#define DC_WIN_COLOR_DEPTH 0x703
#define DC_WIN_POSITION 0x704
#define DC_WIN_SIZE 0x705
#define DC_WIN_STRIDE 0x706
#define DC_WIN_LINE_PARAM 0x707
#define DC_WIN_CSC_YOF 0x708
#define DC_WIN_WIN_OPTIONS 0x700
#define DC_WIN_BYTE_SWAP 0x701
#define DC_WIN_BUF_ADDR 0x702
#define DC_CMD_DISPLAY_COMMAND 0x032
#define DC_CMD_DISPLAY_WINDOW_HEADER 0x034
#define DC_CMD_GENERAL_INCR_SYNCPT 0x000 /* +0x100 per window */
#define DC_WIN_WIN_OPTIONS_WIN_ENABLE BIT(0)
#define DC_CMD_DISPLAY_COMMAND_OPTION0 0x001
#define DC_DISP_DISP_CLOCK_CONTROL_PIXEL_CLK_ENABLE (1 << 3)

#define T30_DSI_BASE 0x54300000u
#define T30_HDMI_BASE 0x54280000u
#define T30_DC_COM_BASE 0x54200000u

/* ------------------------------------------------------------------ USB ---- */
#define T30_USB_BASE(n) (0x7D000000u + (n) * 0x4000u)
#define T30_USB_UTMIP_BASE 0x7D000000u
#define T30_USB_PAD_CTRL(n) (0x7D008000u + (n) * 0x4000u)

/* EHCI */
#define EHCI_CAPLENGTH 0x00
#define EHCI_HCIVERSION 0x02
#define EHCI_HCSPARAMS 0x04
#define EHCI_HCCPARAMS 0x08
#define EHCI_USBCMD 0x10
#define EHCI_USBSTS 0x14
#define EHCI_USBINTR 0x18
#define EHCI_FRINDEX 0x1C
#define EHCI_CTRLDSSEGMENT 0x20
#define EHCI_PERIODICLISTBASE 0x24
#define EHCI_ASYNCLISTADDR 0x28
#define EHCI_PORTSC(n) (0x44 + (n) * 4)
#define EHCI_USBCMD_RUN BIT(0)
#define EHCI_USBCMD_RST BIT(1)
#define EHCI_USBCMD_PSE BIT(4)
#define EHCI_USBCMD_ASE BIT(5)
#define EHCI_USBSTS_HALTED BIT(12)
#define EHCI_PORTSC_CONN BIT(0)
#define EHCI_PORTSC_CONN_CHANGE BIT(1)
#define EHCI_PORTSC_ENABLE BIT(2)
#define EHCI_PORTSC_RESET BIT(8)
#define EHCI_PORTSC_POWER BIT(12)
#define EHCI_PORTSC_OWNER BIT(13)

/* ------------------------------------------------------------- chip IDs ---- */
#define T30_APB_MISC_BASE 0x70000000u
#define APB_MISC_GP_HIDREV 0x804

#endif /* SRTOS_TEGRA30_H */
