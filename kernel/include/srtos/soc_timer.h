/*
 * Per-SoC timer back end.
 *
 * clock.c owns the policy (uptime, tick, delays); the SoC file owns the
 * registers.  Two implementations exist:
 *
 *   arch/arm/timer_a9.c       Cortex-A9 MPCore private + global timers  (T30)
 *   arch/arm/timer_generic.c  ARM Generic Timer, EL1 physical timer      (T114)
 *
 * Both are compiled; the one that does not match SRTOS_SOC_* compiles to an
 * empty translation unit.
 */
#ifndef SRTOS_SOC_TIMER_H
#define SRTOS_SOC_TIMER_H

#include <srtos/types.h>

/* Bring the counter up.  Returns 0 on success. */
int soc_timer_init(u32 cpu_hz);

/* Monotonic free running counter, in ticks. */
u64 soc_timer_now(void);

/* Rate of soc_timer_now(), filled in by clock.c after calibration. */
void soc_timer_set_hz(u32 hz);
u32 soc_timer_get_hz(void);

u32 soc_timer_nominal_hz(void);

/* Start a periodic interrupt at `hz` from the counter. */
int soc_timer_start_tick(u32 hz, void (*isr)(void));

#endif /* SRTOS_SOC_TIMER_H */
