/*
 * SurfaceRT_OS - Maxim MAX77663 PMIC
 *
 * The Surface RT uses a MAX77663 companion PMIC (the "Nexus" rail controller)
 * on I2C bus 4 together with a MAX17047 style fuel gauge.  This driver owns
 * power sequencing (reboot / power off), the RTC and the battery gauge.
 *
 * Register addresses below follow the MAX77663 datasheet: each regulator is a
 * 16 byte block of CNFG/CFG registers, and the global block holds the RTC and
 * the power sequencing controls.
 */
#include <srtos/pmic.h>
#include <srtos/i2c.h>
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/soc.h>

#define PMIC_I2C_BUS PLATFORM_PMIC_I2C_BUS
#define PMIC_ADDR PLATFORM_PMIC_I2C_ADDR

/* Global / miscellaneous block */
#define PMIC_REG_IRQ_MASK 0x00
#define PMIC_REG_STATUS 0x01
#define PMIC_REG_CNFG_GLBL 0x10
#define PMIC_REG_SLP_CNFG 0x11
#define PMIC_REG_ONOFF_CNFG 0x12
#define PMIC_REG_RTC_UPDATE0 0x14
#define PMIC_REG_RTC_UPDATE1 0x15
#define PMIC_REG_RTC0 0x16 /* seconds, LSB first through 0x1B */
#define PMIC_REG_RTC1 0x17

#define PMIC_GLBL_SUSPEND 0x01
#define PMIC_ONOFF_OFF 0x04
#define PMIC_ONOFF_RESET 0x08

static int g_present;
static u32 g_rtc_base;

static int pmic_present_probe(void)
{
    u8 id = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (i2c_read_reg(PMIC_I2C_BUS, PMIC_ADDR, PMIC_REG_STATUS, &id) == 0) {
            return 1;
        }
        mdelay(2);
    }
    return 0;
}

int pmic_init(void)
{
    u8 v;

    g_present = pmic_present_probe();
    if (!g_present) {
        pr_warn("pmic: MAX77663 not found on i2c%d/0x%02x\n", PMIC_I2C_BUS,
                PMIC_ADDR);
        return -1;
    }
    if (i2c_read_reg(PMIC_I2C_BUS, PMIC_ADDR, PMIC_REG_CNFG_GLBL, &v) == 0) {
        pr_info("pmic: MAX77663 ready (glbl=0x%02x)\n", v);
    } else {
        pr_info("pmic: MAX77663 ready\n");
    }
    g_rtc_base = 1400000000u; /* fallback epoch until the RTC is read */
    return 0;
}

int pmic_read_reg(int reg, u8 *val)
{
    if (!g_present) {
        return -1;
    }
    return i2c_read_reg(PMIC_I2C_BUS, PMIC_ADDR, (u8)reg, val);
}

int pmic_write_reg(int reg, u8 val)
{
    if (!g_present) {
        return -1;
    }
    return i2c_write_reg(PMIC_I2C_BUS, PMIC_ADDR, (u8)reg, val);
}

void pmic_poweroff_prepare(void)
{
    if (!g_present) {
        return;
    }
    /* disable the "on" button wake sources so the PMIC does not fight us */
    pmic_write_reg(PMIC_REG_ONOFF_CNFG,
                   (u8)(PMIC_ONOFF_OFF | PMIC_ONOFF_RESET));
    mdelay(5);
}

int pmic_poweroff(void)
{
    if (!g_present) {
        return -1;
    }
    return pmic_write_reg(PMIC_REG_ONOFF_CNFG, PMIC_ONOFF_OFF);
}

int pmic_reboot(void)
{
    if (!g_present) {
        return -1;
    }
    return pmic_write_reg(PMIC_REG_ONOFF_CNFG, PMIC_ONOFF_RESET);
}

int pmic_read_rtc(u32 *seconds)
{
    u8 b[4];
    if (!g_present) {
        return -1;
    }
    /* freeze the RTC counter, read 4 bytes, release */
    if (pmic_write_reg(PMIC_REG_RTC_UPDATE0, 0x01) != 0) {
        return -1;
    }
    if (i2c_read_regs(PMIC_I2C_BUS, PMIC_ADDR, PMIC_REG_RTC0, b, 4) != 0) {
        pmic_write_reg(PMIC_REG_RTC_UPDATE0, 0x00);
        return -1;
    }
    pmic_write_reg(PMIC_REG_RTC_UPDATE0, 0x00);
    *seconds = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) |
               ((u32)b[3] << 24);
    return 0;
}

int pmic_set_rtc(u32 seconds)
{
    u8 b[4];
    if (!g_present) {
        return -1;
    }
    b[0] = (u8)(seconds);
    b[1] = (u8)(seconds >> 8);
    b[2] = (u8)(seconds >> 16);
    b[3] = (u8)(seconds >> 24);
    if (i2c_write_regs(PMIC_I2C_BUS, PMIC_ADDR, PMIC_REG_RTC0, b, 4) != 0) {
        return -1;
    }
    return pmic_write_reg(PMIC_REG_RTC_UPDATE1, 0x01);
}

int pmic_battery_percent(void)
{
    /* The fuel gauge lives at 0x36 on the same bus (MAX17047 compatible). */
    u8 soc = 0;
    if (i2c_read_regs(PMIC_I2C_BUS, 0x36, 0x04, &soc, 1) != 0) {
        return -1;
    }
    return soc;
}

void pmic_dump(void)
{
    u8 v;
    printk("pmic: MAX77663 %s\n", g_present ? "present" : "absent");
    if (pmic_read_reg(PMIC_REG_STATUS, &v) == 0) {
        printk("pmic: status=0x%02x battery=%d%%\n", v, pmic_battery_percent());
    }
}
