/*
 * SurfaceRT_OS - Tegra 3 I2C controller
 *
 * Polled single message transfers; enough for the PMIC, the fuel gauge and the
 * panel EEPROM.  Every transaction is bounded by a timeout so a stuck bus can
 * never wedge the kernel.
 */
#include <srtos/i2c.h>
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include <srtos/soc.h>

#define I2C_TIMEOUT_US 20000
#define I2C_BUSES 5

static int g_inited;

int i2c_init(void)
{
    int bus;
    for (bus = 0; bus < I2C_BUSES; bus++) {
        uptr base = TEGRA_I2C_BASE(bus);
        writel(0, base + I2C_CNFG);
        writel(0x1E, base + I2C_CLK_DIV);
        writel(0xFFFFFFFFu, base + I2C_INTR_MASK);
        writel(I2C_CNFG_NEW_MASTER | I2C_CNFG_DEBOUNCE, base + I2C_CNFG);
    }
    g_inited = 1;
    pr_info("i2c: %d buses at 0x%08x\n", I2C_BUSES, TEGRA_I2C_BASE(0));
    return 0;
}

static int i2c_wait_idle(uptr base)
{
    u32 start = readl(TEGRA_TMR_BASE + TMR_USEC_CNTR);
    while (readl(base + I2C_STATUS) & I2C_STATUS_BUSY) {
        if ((u32)(readl(TEGRA_TMR_BASE + TMR_USEC_CNTR) - start) > I2C_TIMEOUT_US) {
            return -1;
        }
    }
    return 0;
}

static int i2c_transfer(int bus, u8 addr, u8 reg, u8 *buf, u32 len, int read)
{
    uptr base;
    u32 cmd = 0, data = 0;
    int i;

    if (!g_inited || bus < 0 || bus >= I2C_BUSES || len == 0 || len > 4) {
        return -1;
    }
    base = TEGRA_I2C_BASE(bus);
    if (i2c_wait_idle(base) != 0) {
        return -1;
    }
    writel(0xFFFFFFFFu, base + I2C_INTR_STATUS);
    writel(((u32)addr << 1) | (read ? 1u : 0u), base + I2C_CMD_ADDR0);

    if (!read) {
        for (i = 0; i < (int)len; i++) {
            data |= (u32)buf[i] << (8 * i);
        }
        /* CNFG[3:1] holds (bytes - 1) for CMD1 */
        cmd = (u32)(len - 1) << 1;
        writel(data, base + I2C_CMD_DATA1);
        writel(I2C_CNFG_NEW_MASTER | I2C_CNFG_DEBOUNCE | I2C_CNFG_CMD1 |
                   I2C_CNFG_START | cmd,
               base + I2C_CNFG);
    } else {
        /* register address byte, then a repeated start read */
        writel((u32)reg, base + I2C_CMD_DATA1);
        writel(I2C_CNFG_NEW_MASTER | I2C_CNFG_DEBOUNCE | (0u << 1) |
                   I2C_CNFG_CMD1 | I2C_CNFG_START,
               base + I2C_CNFG);
        if (i2c_wait_idle(base) != 0) {
            return -1;
        }
        writel(((u32)addr << 1) | 1u, base + I2C_CMD_ADDR0);
        writel((((u32)(len - 1)) << 1) | I2C_CMD_READ, base + I2C_CMD_DATA1);
        writel(I2C_CNFG_NEW_MASTER | I2C_CNFG_DEBOUNCE | I2C_CNFG_CMD1 |
                   I2C_CNFG_START,
               base + I2C_CNFG);
    }

    if (i2c_wait_idle(base) != 0) {
        return -1;
    }
    if (readl(base + I2C_STATUS) & I2C_STATUS_NOACK) {
        return -1;
    }
    if (read) {
        data = readl(base + I2C_CMD_DATA1);
        for (i = 0; i < (int)len; i++) {
            buf[i] = (u8)(data >> (8 * i));
        }
    }
    return 0;
}

int i2c_write_regs(int bus, u8 addr, u8 reg, const u8 *buf, u32 len)
{
    u8 tmp[4];
    if (len > sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, buf, len);
    return i2c_transfer(bus, addr, reg, tmp, len, 0);
}

int i2c_read_regs(int bus, u8 addr, u8 reg, u8 *buf, u32 len)
{
    return i2c_transfer(bus, addr, reg, buf, len, 1);
}

int i2c_read_reg(int bus, u8 addr, u8 reg, u8 *val)
{
    return i2c_read_regs(bus, addr, reg, val, 1);
}

int i2c_write_reg(int bus, u8 addr, u8 reg, u8 val)
{
    return i2c_write_regs(bus, addr, reg, &val, 1);
}

void i2c_dump(void)
{
    int bus;
    for (bus = 0; bus < I2C_BUSES; bus++) {
        printk("i2c%d: status=0x%08x cnfg=0x%08x\n", bus,
               readl(TEGRA_I2C_BASE(bus) + I2C_STATUS),
               readl(TEGRA_I2C_BASE(bus) + I2C_CNFG));
    }
}
