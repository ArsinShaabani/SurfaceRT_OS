/* SurfaceRT_OS - time and timers */
#ifndef SRTOS_CLOCK_H
#define SRTOS_CLOCK_H

#include <srtos/types.h>
#include <srtos/bootinfo.h>

#define CONFIG_HZ 1000

typedef struct {
    u32 seconds;
    u32 nanos;
} ktime_t;

void clock_init(const struct srt_bootinfo *bi);
u64 clock_uptime_us(void);
u64 clock_uptime_ms(void);
void clock_gettime(ktime_t *ts);
void udelay(u32 us);
void mdelay(u32 ms);
u32 clock_hz(void);
void clock_dump(void);

#endif
