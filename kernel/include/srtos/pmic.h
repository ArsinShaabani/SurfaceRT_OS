/* SurfaceRT_OS - MAX77663 PMIC (Surface RT power, RTC, battery) */
#ifndef SRTOS_PMIC_H
#define SRTOS_PMIC_H

#include <srtos/types.h>

int pmic_init(void);
int pmic_poweroff(void);
void pmic_poweroff_prepare(void);
int pmic_reboot(void);
int pmic_read_rtc(u32 *seconds);
int pmic_set_rtc(u32 seconds);
int pmic_battery_percent(void);
int pmic_read_reg(int reg, u8 *val);
int pmic_write_reg(int reg, u8 val);
void pmic_dump(void);

#endif
