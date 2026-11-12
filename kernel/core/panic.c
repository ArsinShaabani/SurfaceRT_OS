/* SurfaceRT_OS - panic, reboot and power off */
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/clock.h>
#include <srtos/pmic.h>
#include <srtos/string.h>
#include "tegra30.h"

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
    writel(readl(T30_PMC_BASE + PMC_CNTRL) | PMC_CNTRL_MAIN_RST,
           T30_PMC_BASE + PMC_CNTRL);
    mdelay(50);
    /* fall back to the A9 watchdog */
    writel(0, T30_A9_WATCHDOG + 0x08);
    writel(0xFFFFFFFFu, T30_A9_WATCHDOG + 0x00);
    writel(BIT(0) | BIT(1) | BIT(2) | BIT(3), T30_A9_WATCHDOG + 0x08);
    for (;;) {
        wfi();
    }
}

void poweroff_now(void)
{
    printk("pmc: powering off\n");
    pmic_poweroff_prepare();
    writel(readl(T30_PMC_BASE + PMC_CNTRL) | PMC_CNTRL_PWRREQ_OE,
           T30_PMC_BASE + PMC_CNTRL);
    /* pull the power request line to the PMIC */
    pmic_poweroff();
    mdelay(100);
    /* last resort: hold the CPU in reset */
    for (;;) {
        wfi();
    }
}
