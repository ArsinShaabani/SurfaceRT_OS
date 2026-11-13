/* SurfaceRT_OS - panic, reboot and power off */
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/clock.h>
#include <srtos/pmic.h>
#include <srtos/string.h>
#include <srtos/soc.h>

static volatile int g_panicked;

void panic(const char *fmt, ...)
{
    char buf[400];
    va_list ap;

    __asm__ __volatile__("cpsid aif" ::: "memory");
    g_panicked = 1;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printk("\n<0>KERNEL PANIC: %s\n", buf);
    printk("<0>  cpsr=0x%08x  cpu=%u  uptime=%llu ms\n", arm_read_cpsr(),
           arm_cpu_id(), clock_uptime_ms());
    printk("<0>system halted.  hold the power button to reset.\n");

    for (;;) {
        wfi();
    }
}

void reboot_now(void)
{
    printk("pmc: rebooting\n");
    pmic_poweroff_prepare();
    /* PMC main reset */
    writel(readl(TEGRA_PMC_BASE + PMC_CNTRL) | PMC_CNTRL_MAIN_RST,
           TEGRA_PMC_BASE + PMC_CNTRL);
    mdelay(50);
#if defined(SRTOS_SOC_TEGRA30)
    /* Cortex-A9 has a per-CPU watchdog we can kick as a last resort. */
    writel(0, TEGRA_A9_WATCHDOG + 0x08);
    writel(0xFFFFFFFFu, TEGRA_A9_WATCHDOG + 0x00);
    writel(BIT(0) | BIT(1) | BIT(2) | BIT(3), TEGRA_A9_WATCHDOG + 0x08);
#else
    /* Cortex-A15 has no per-CPU watchdog; the PMC master reset above is the
     * documented path on Tegra 4 (see the PMC chapter of the TRM). */
#endif
    for (;;) {
        wfi();
    }
}

void poweroff_now(void)
{
    printk("pmc: powering off\n");
    pmic_poweroff_prepare();
    writel(readl(TEGRA_PMC_BASE + PMC_CNTRL) | PMC_CNTRL_PWRREQ_OE,
           TEGRA_PMC_BASE + PMC_CNTRL);
    /* pull the power request line to the PMIC */
    pmic_poweroff();
    mdelay(100);
    /* last resort: hold the CPU in reset */
    for (;;) {
        wfi();
    }
}
