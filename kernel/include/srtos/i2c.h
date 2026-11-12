/* SurfaceRT_OS - Tegra I2C */
#ifndef SRTOS_I2C_H
#define SRTOS_I2C_H

#include <srtos/types.h>

int i2c_init(void);
int i2c_read_regs(int bus, u8 addr, u8 reg, u8 *buf, u32 len);
int i2c_write_regs(int bus, u8 addr, u8 reg, const u8 *buf, u32 len);
int i2c_read_reg(int bus, u8 addr, u8 reg, u8 *val);
int i2c_write_reg(int bus, u8 addr, u8 reg, u8 val);
void i2c_dump(void);

#endif
