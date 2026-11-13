/*
 * SurfaceRT_OS - timekeeping policy
 *
 * clock.c owns uptime, delays and the scheduler tick.  The actual counter comes
 * from the SoC timer back end (soc_timer.h): the Cortex-A9 timers on the
 * Tegra 3, the ARM generic timer on the Cortex-A15 in the Tegra 4.
 *
 * Nothing here trusts a documented clock rate.  The counter is *measured*
 * against the Tegra TMR microsecond counter (0x60005000), which the firmware
 * leaves running at 1 MHz on every Tegra.  If that reference is dead the
 * counter's own declared rate is used; if even that is missing, uptime falls
 * back to the tick counter (coarse but never wrong by more than one tick).
 */
#include <srtos/clock.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/sched.h>
#include <srtos/soc.h>
#include <srtos/soc_timer.h>
#include <srtos/string.h>

static u32 g_counter_hz;   /* measured rate of soc_timer_now(), 0 = unusable */
static u64 g_counter_start;
static u32 g_cpu_hz = 1300000000u;
static u32 g_have_usec_ref;
static volatile u64 g_ticks;

/* ------------------------------------------------------------- reference --- */
static inline u32 tmr_us(void)
{
    return readl(TEGRA_TMR_BASE + TMR_USEC_CNTR);
}

static int usec_reference_probe(void)
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

/* Measure the free running counter against the 1 MHz reference. */
static u32 measure_counter_hz(void)
{
    u64 t0, t1;
    u32 us0, us1, delta_us, delta_ticks;

    if (!g_have_usec_ref) {
        return 0;
    }
    t0 = soc_timer_now();
    us0 = tmr_us();
    busy_until_us(5000);
    t1 = soc_timer_now();
    us1 = tmr_us();
    delta_us = us1 - us0;
    if (!delta_us || delta_us > 1000000u) {
        return 0;
    }
    if (t1 <= t0) {
        return 0; /* counter is not running */
    }
    {
        u64 delta = t1 - t0;
        u64 hz = (delta * 1000000ull) / delta_us;
        if (hz > 0xFFFFFFFFull) {
            return 0;
        }
        delta_ticks = (u32)hz;
    }
    return delta_ticks;
}

/* ------------------------------------------------------------------ tick --- */
static void tick_isr(void)
{
    g_ticks++;
    sched_tick();
}

void clock_init(const struct srt_bootinfo *bi)
{
    u32 measured, nominal;

    if (bi && bi->cpu_clock_hz) {
        g_cpu_hz = (u32)bi->cpu_clock_hz;
    }
    g_have_usec_ref = usec_reference_probe();
    pr_info("clock: tegra 1MHz reference %s\n",
            g_have_usec_ref ? "found" : "not usable");

    if (soc_timer_init(g_cpu_hz) != 0) {
        pr_err("clock: no usable counter, uptime will follow the tick\n");
        g_counter_hz = 0;
    } else {
        g_counter_start = soc_timer_now();
        measured = measure_counter_hz();
        nominal = soc_timer_nominal_hz();
        if (!measured) {
            measured = nominal;
            if (measured) {
                pr_warn("clock: counter rate taken from the SoC (%u Hz)\n",
                        measured);
            }
        }
        if (measured && (measured < 100000u || measured > 4000000000u)) {
            pr_warn("clock: implausible counter rate %u Hz, ignored\n", measured);
            measured = 0;
        }
        if (measured && soc_timer_now() == g_counter_start) {
            pr_warn("clock: counter does not advance, ignored\n");
            measured = 0;
        }
        g_counter_hz = measured;
        if (g_counter_hz) {
            soc_timer_set_hz(g_counter_hz);
        }
    }

    if (soc_timer_start_tick(CONFIG_HZ, tick_isr) != 0) {
        pr_err("clock: cannot start the scheduler tick\n");
    }
    pr_info("clock: %s, cpu %u MHz, counter %u.%03u MHz, tick %u Hz\n",
            PLATFORM_TIMER_NAME, g_cpu_hz / 1000000u,
            g_counter_hz / 1000000u, (g_counter_hz / 1000u) % 1000u, CONFIG_HZ);
}

u64 clock_uptime_us(void)
{
    if (g_counter_hz) {
        u64 delta = soc_timer_now() - g_counter_start;
        return (delta * 1000000ull) / g_counter_hz;
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
    printk("clock: %s, uptime %llu us, ticks %llu, counter %u Hz, cpu %u MHz\n",
           PLATFORM_TIMER_NAME, clock_uptime_us(), g_ticks, g_counter_hz,
           g_cpu_hz / 1000000u);
}
