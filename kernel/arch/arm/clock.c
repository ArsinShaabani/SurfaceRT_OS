/*
 * SurfaceRT_OS - timekeeping
 *
 * Three timers are used, in this order of preference:
 *
 *   Cortex-A9 global timer  (0x50040200) - free running 64 bit counter, used
 *                                          for uptime and delays.
 *   Cortex-A9 private timer (0x50040600) - per-CPU down counter, drives the
 *                                          scheduler tick through PPI 29.
 *   Tegra TMR 1 MHz counter (0x60005000) - only used to *calibrate* the two
 *                                          A9 timers at boot.
 *
 * Nothing here guesses a clock rate: every frequency is measured against the
 * Tegra microsecond counter, which the firmware leaves running at 1 MHz.  If
 * even that is dead we fall back to the 1.3 GHz T30 CPU clock / 2, which is the
 * documented PERIPHCLK for these timers.
 */
#include <srtos/clock.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/sched.h>
#include <srtos/string.h>
#include "tegra30.h"

#define IRQ_A9_PRIVATE_TIMER 29
#define IRQ_A9_GLOBAL_TIMER 27

static u32 g_gtmr_hz;      /* 0 when the global timer is unusable */
static u32 g_ptmr_hz;
static u32 g_usec_counter; /* Tegra TMR usable as a 1 MHz reference */
static u64 g_gtmr_start;
static volatile u64 g_ticks;
static u32 g_cpu_hz = 1300000000u;

static inline u32 gtmr_lo(void)
{
    return readl(T30_A9_GLOBAL_TIMER + A9_GTMR_COUNTER_LO);
}

static inline u32 gtmr_hi(void)
{
    return readl(T30_A9_GLOBAL_TIMER + A9_GTMR_COUNTER_HI);
}

static u64 gtmr_read(void)
{
    u32 hi, lo, hi2;
    do {
        hi = gtmr_hi();
        lo = gtmr_lo();
        hi2 = gtmr_hi();
    } while (hi != hi2);
    return ((u64)hi << 32) | lo;
}

static u32 tmr_us(void)
{
    return readl(T30_TMR_BASE + TMR_USEC_CNTR);
}

static u32 ptmr_count(void)
{
    return readl(T30_A9_PRIVATE_TIMER + A9_TMR_COUNTER);
}

static int usec_counter_probe(void)
{
    u32 a = tmr_us();
    volatile u32 spin = 2000;
    while (spin--) {
        __asm__ __volatile__("nop");
    }
    return tmr_us() != a;
}

static void busy_until_us(u32 us)
{
    u32 start = tmr_us();
    while ((u32)(tmr_us() - start) < us) {
        __asm__ __volatile__("nop");
    }
}

/* Measure the A9 global timer against the 1 MHz reference. */
static u32 calibrate_gtmr(void)
{
    u64 t0, t1;
    u32 us0, us1, delta_us;
    u64 delta;

    if (!g_usec_counter) {
        return g_cpu_hz / 2;
    }
    t0 = gtmr_read();
    us0 = tmr_us();
    busy_until_us(5000);
    t1 = gtmr_read();
    us1 = tmr_us();
    delta_us = us1 - us0;
    delta = t1 - t0;
    if (!delta_us || delta_us > 1000000) {
        return g_cpu_hz / 2;
    }
    return (u32)((delta * 1000000ull) / delta_us);
}

static void tick_handler(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    /* clear the private timer event flag */
    writel(A9_TMR_ISR, T30_A9_PRIVATE_TIMER + A9_TMR_ISR);
    g_ticks++;
    sched_tick();
}

void clock_init(const struct srt_bootinfo *bi)
{
    u32 reload_us;

    if (bi && bi->cpu_clock_hz) {
        g_cpu_hz = (u32)bi->cpu_clock_hz;
    }
    g_usec_counter = usec_counter_probe();
    pr_info("clock: tegra 1MHz counter %s\n",
            g_usec_counter ? "found" : "not usable, guessing");

    /* --- global timer (uptime) --- */
    writel(A9_GTMR_CTRL_ENABLE | A9_GTMR_CTRL_AUTONC,
           T30_A9_GLOBAL_TIMER + A9_GTMR_CONTROL);
    __asm__ __volatile__("dsb");
    if (gtmr_read() != 0 || 1) {
        g_gtmr_hz = calibrate_gtmr();
        if (g_gtmr_hz < 1000000u || g_gtmr_hz > 4000000000u) {
            pr_warn("clock: implausible global timer rate %u Hz, disabled\n",
                    g_gtmr_hz);
            g_gtmr_hz = 0;
        }
    }
    g_gtmr_start = g_gtmr_hz ? gtmr_read() : 0;

    /* --- private timer (scheduler tick) --- */
    writel(0, T30_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    writel(0xFFFFFFFFu, T30_A9_PRIVATE_TIMER + A9_TMR_LOAD);
    writel(A9_TMR_CTRL_ENABLE, T30_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    if (g_usec_counter) {
        u32 c0 = ptmr_count();
        u32 us0 = tmr_us();
        busy_until_us(5000);
        u32 c1 = ptmr_count();
        u32 us1 = tmr_us();
        u32 du = us1 - us0;
        if (du) {
            g_ptmr_hz = (u32)(((u64)(c0 - c1) * 1000000ull) / du);
        }
    }
    if (!g_ptmr_hz || g_ptmr_hz < 1000000u) {
        g_ptmr_hz = g_cpu_hz / 2;
    }
    reload_us = 1000000u / CONFIG_HZ;
    writel(0, T30_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    writel((g_ptmr_hz / CONFIG_HZ) - 1, T30_A9_PRIVATE_TIMER + A9_TMR_LOAD);
    writel(A9_TMR_ISR, T30_A9_PRIVATE_TIMER + A9_TMR_ISR);
    writel(A9_TMR_CTRL_ENABLE | A9_TMR_CTRL_AUTORELOAD | A9_TMR_CTRL_IRQEN,
           T30_A9_PRIVATE_TIMER + A9_TMR_CONTROL);
    irq_register(IRQ_A9_PRIVATE_TIMER, tick_handler, 0);

    pr_info("clock: cpu %u MHz, global timer %u MHz, tick %u Hz (%u us)\n",
            g_cpu_hz / 1000000u, g_gtmr_hz / 1000000u, CONFIG_HZ, reload_us);
}

u64 clock_uptime_us(void)
{
    if (g_gtmr_hz) {
        u64 delta = gtmr_read() - g_gtmr_start;
        return (delta * 1000000ull) / g_gtmr_hz;
    }
    return g_ticks * (1000000u / CONFIG_HZ);
}

u64 clock_uptime_ms(void)
{
    return clock_uptime_us() / 1000ull;
}

void clock_gettime(ktime_t *ts)
{
    u64 us = clock_uptime_us();
    ts->seconds = (u32)(us / 1000000ull);
    ts->nanos = (u32)((us % 1000000ull) * 1000ull);
}

void udelay(u32 us)
{
    u64 start = clock_uptime_us();
    if (us > 1000000u) {
        us = 1000000u;
    }
    while ((clock_uptime_us() - start) < us) {
        __asm__ __volatile__("nop");
    }
}

void mdelay(u32 ms)
{
    while (ms > 1000u) {
        udelay(1000000u);
        ms -= 1000u;
    }
    udelay(ms * 1000u);
}

u32 clock_hz(void)
{
    return CONFIG_HZ;
}

void clock_dump(void)
{
    printk("clock: uptime %llu us, ticks %llu, gtmr %u Hz, ptmr %u Hz\n",
           clock_uptime_us(), g_ticks, g_gtmr_hz, g_ptmr_hz);
}
