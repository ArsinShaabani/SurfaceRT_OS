/*
 * Cortex-A9 MPCore timers - the Tegra 3 (T30) time source.
 *
 *   global timer  : free running 64 bit counter, drives uptime
 *   private timer : per-CPU down counter, drives the scheduler tick (PPI 29)
 *
 * The rates are measured by clock.c against the Tegra 1 MHz counter and handed
 * back through soc_timer_set_hz(); nothing here assumes a PERIPHCLK value.
 */
#include <srtos/clock.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/soc.h>
#include <srtos/soc_timer.h>

#if defined(SRTOS_SOC_TEGRA30)

static u32 g_counter_hz; /* measured rate of the global timer */
static u64 g_counter_base;
static void (*g_tick_isr)(void);

static void a9_tick(void);

int soc_timer_init(u32 cpu_hz)
{
    (void)cpu_hz;
    /* global timer: enable, free running, no prescaler */
    writel(A9_GTMR_CTRL_ENABLE | A9_GTMR_CTRL_AUTONC,
           TEGRA_A9_GLOBAL_TIMER + A9_GTMR_CONTROL);
    __asm__ __volatile__("dsb");
    g_counter_base = 0;

    /* private timer: free running while clock.c measures it */
    writel(0, TEGRA_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    writel(0xFFFFFFFFu, TEGRA_A9_PRIVATE_TIMER + A9_TMR_LOAD);
    writel(A9_TMR_CTRL_ENABLE, TEGRA_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    return 0;
}

u64 soc_timer_now(void)
{
    u32 hi, lo, hi2;
    do {
        hi = readl(TEGRA_A9_GLOBAL_TIMER + A9_GTMR_COUNTER_HI);
        lo = readl(TEGRA_A9_GLOBAL_TIMER + A9_GTMR_COUNTER_LO);
        hi2 = readl(TEGRA_A9_GLOBAL_TIMER + A9_GTMR_COUNTER_HI);
    } while (hi != hi2);
    return ((u64)hi << 32) | lo;
}

void soc_timer_set_hz(u32 hz)
{
    g_counter_hz = hz;
}

u32 soc_timer_get_hz(void)
{
    return g_counter_hz;
}

u32 soc_timer_nominal_hz(void)
{
    /* documented T30 PERIPHCLK: CPU clock / 2 */
    return 0;
}

static void a9_tick_irq(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    a9_tick();
}

static void a9_tick(void)
{
    /* clear the event flag, then let clock.c do the bookkeeping */
    writel(A9_TMR_ISR, TEGRA_A9_PRIVATE_TIMER + A9_TMR_ISR);
    if (g_tick_isr) {
        g_tick_isr();
    }
}

int soc_timer_start_tick(u32 hz, void (*isr)(void))
{
    u32 private_hz = g_counter_hz;

    if (!hz) {
        return -1;
    }
    if (!private_hz) {
        private_hz = 1300000000u / 2; /* last resort: documented PERIPHCLK */
    }
    g_tick_isr = isr;

    writel(0, TEGRA_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    writel((private_hz / hz) - 1, TEGRA_A9_PRIVATE_TIMER + A9_TMR_LOAD);
    writel(A9_TMR_ISR, TEGRA_A9_PRIVATE_TIMER + A9_TMR_ISR);
    writel(A9_TMR_CTRL_ENABLE | A9_TMR_CTRL_AUTORELOAD | A9_TMR_CTRL_IRQEN,
           TEGRA_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    return irq_register(PLATFORM_IRQ_TIMER, a9_tick_irq, 0);
}

#endif /* SRTOS_SOC_TEGRA30 */
