/*
 * ARM Generic Timer - the time source for the Cortex-A15 in the Tegra 4 (T114).
 *
 * The Cortex-A9 timers used by the Tegra 3 port do not exist on the A15.  What
 * the A15 (and every ARMv7-A core with the generic timer) offers instead is:
 *
 *   CNTFRQ      counter frequency, programmed by the firmware
 *   CNTPCT      free running 64 bit physical counter
 *   CNTP_TVAL   down counter that raises an interrupt when it reaches zero
 *   CNTP_CTL    enable / mask / status of that interrupt
 *
 * The tick uses the non-secure EL1 physical timer, which is PPI 30 on a
 * GIC-400.  For EL1 to be allowed to touch the timer at all the hypervisor
 * (if the firmware left us at EL2) must set CNTHCTL.EL1PCEN - arch/arm/start.S
 * does that while dropping from EL2 to EL1.
 */
#include <srtos/clock.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/soc.h>
#include <srtos/soc_timer.h>

#if defined(SRTOS_SOC_TEGRA114)

static u32 g_rate_hz;   /* measured counter rate (clock.c) */
static u32 g_tick_hz = 1000;
static void (*g_tick_isr)(void);

static inline u32 cntfrq_read(void)
{
    u32 v;
    __asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

static inline u64 cntpct_read(void)
{
    u32 lo, hi;
    __asm__ __volatile__("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((u64)hi << 32) | lo;
}

static inline void cntp_tval_write(u32 v)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 0" : : "r"(v));
}

static inline u32 cntp_ctl_read(void)
{
    u32 v;
    __asm__ __volatile__("mrc p15, 0, %0, c14, c2, 1" : "=r"(v));
    return v;
}

static inline void cntp_ctl_write(u32 v)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c2, 1" : : "r"(v));
}

int soc_timer_init(u32 cpu_hz)
{
    (void)cpu_hz;
    /* stop the timer and clear any pending condition before we use it */
    cntp_ctl_write(0);
    cntp_tval_write(0x7FFFFFFFu);
    g_rate_hz = cntfrq_read();
    pr_info("timer: ARM generic timer, CNTFRQ %u Hz, EL1 physical timer (PPI %d)\n",
            g_rate_hz, PLATFORM_IRQ_TIMER);
    return 0;
}

u64 soc_timer_now(void)
{
    return cntpct_read();
}

void soc_timer_set_hz(u32 hz)
{
    g_rate_hz = hz;
}

u32 soc_timer_get_hz(void)
{
    return g_rate_hz;
}

u32 soc_timer_nominal_hz(void)
{
    return cntfrq_read();
}

static void generic_reload(void)
{
    u32 ticks = g_rate_hz / (g_tick_hz ? g_tick_hz : 1000);

    if (!ticks) {
        ticks = 1;
    }
    cntp_tval_write(ticks);
}

static void generic_tick_irq(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    /* a generic timer tick is one shot: re-arm before doing any work */
    generic_reload();
    if (g_tick_isr) {
        g_tick_isr();
    }
}

int soc_timer_start_tick(u32 hz, void (*isr)(void))
{
    if (!hz) {
        return -1;
    }
    g_tick_hz = hz;
    g_tick_isr = isr;
    if (!g_rate_hz) {
        g_rate_hz = cntfrq_read();
    }
    if (!g_rate_hz) {
        pr_err("timer: CNTFRQ is zero and nothing was measured\n");
        return -1;
    }
    generic_reload();
    /* ENABLE=1, IMASK=0 (interrupt enabled) */
    cntp_ctl_write(1);
    return irq_register(PLATFORM_IRQ_TIMER, generic_tick_irq, 0);
}

#endif /* SRTOS_SOC_TEGRA114 */
