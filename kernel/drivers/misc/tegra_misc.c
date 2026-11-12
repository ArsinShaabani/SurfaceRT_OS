/*
 * SurfaceRT_OS - Tegra 3 odds and ends: fuse/chip ID and the thermal sensor.
 */
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include "tegra30.h"

static u32 g_chip_id;
static u32 g_chip_rev;

int fuse_init(void)
{
    u32 hidrev = readl(T30_APB_MISC_BASE + APB_MISC_GP_HIDREV);
    g_chip_id = (hidrev >> 8) & 0xFF;    /* 0x30 = Tegra 3 (T30) */
    g_chip_rev = hidrev & 0xF;
    pr_info("fuse: SoC id 0x%02x (Tegra 3), revision %u.%u\n", g_chip_id,
            (g_chip_rev >> 4) & 0xF, g_chip_rev & 0xF);
    return 0;
}

int thermal_init(void)
{
    /* Tegra 3 exposes the on-die temperature through the FUSE/TSENSOR block;
     * the raw value is only meaningful with the per-SKU calibration table from
     * the fuse block, so we only log that the block is alive. */
    u32 raw = readl(0x7000E000u + 0x00);
    pr_info("thermal: TSENSOR block readable (raw 0x%08x)\n", raw);
    return 0;
}

/* A simple divider for the PMIC/DC drivers: (clk + div/2) / div */
u32 tegra_div_round(u32 clk, u32 div)
{
    return div ? (clk + div / 2) / div : 0;
}
