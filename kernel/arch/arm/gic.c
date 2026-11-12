/*
 * SurfaceRT_OS - GIC-400 (ARM PL390 compatible) driver
 *
 * Tegra 3 wires the distributor at 0x50041000 and the CPU interface at
 * 0x50042000.  PPI 27/29 are the Cortex-A9 global and private timers.
 */
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/string.h>
#include "tegra30.h"

#define GICD_CTLR 0x000
#define GICD_TYPER 0x004
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ISPENDR 0x200
#define GICD_ICPENDR 0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR 0x800
#define GICD_ICFGR 0xC00

#define GICC_CTLR 0x000
#define GICC_PMR 0x004
#define GICC_BPR 0x008
#define GICC_IAR 0x00C
#define GICC_EOIR 0x010
#define GICC_RPR 0x014
#define GICC_HPPIR 0x018

#define GICD ((volatile u32 *)T30_GIC_DIST_BASE)
#define GICC ((volatile u32 *)T30_GIC_CPU_BASE)

#define SPURIOUS_IRQ 1023

struct irq_entry {
    irq_handler_fn fn;
    void *arg;
    u32 count;
};

static struct irq_entry irq_table[IRQ_NR];

void irq_init(void)
{
    gic_init();
}

int gic_init(void)
{
    u32 i, lines;

    /* disable everything, then configure */
    GICD[GICD_CTLR / 4] = 0;
    GICC[GICC_CTLR / 4] = 0;

    lines = 32u * ((GICD[GICD_TYPER / 4] & 0x1Fu) + 1u);
    if (lines > 160) {
        lines = 160;
    }
    for (i = 0; i < lines; i += 32) {
        GICD[(GICD_ICENABLER + i / 8) / 4] = 0xFFFFFFFFu;
        GICD[(GICD_ICPENDR + i / 8) / 4] = 0xFFFFFFFFu;
    }
    for (i = 0; i < lines; i += 4) {
        GICD[(GICD_IPRIORITYR + i) / 4] = 0xA0A0A0A0u;
        /* SPIs go to CPU 0; PPIs/SGIs have a read-only target field */
        if (i >= 32) {
            GICD[(GICD_ITARGETSR + i) / 4] = 0x01010101u;
        }
    }

    GICC[GICC_PMR / 4] = 0xF0;   /* allow priorities 0x00..0xEF */
    GICC[GICC_BPR / 4] = 0;      /* 7 bits of priority grouping  */
    GICC[GICC_CTLR / 4] = 1;     /* enable the CPU interface     */
    GICD[GICD_CTLR / 4] = 1;     /* enable the distributor       */
    __asm__ __volatile__("dsb");

    pr_info("gic: %u interrupt lines\n", lines);
    return 0;
}

void gic_enable_irq(int irq)
{
    if (irq < 0 || irq >= IRQ_NR) {
        return;
    }
    GICD[(GICD_ISENABLER + (irq / 32) * 4) / 4] = BIT(irq % 32);
}

void gic_disable_irq(int irq)
{
    if (irq < 0 || irq >= IRQ_NR) {
        return;
    }
    GICD[(GICD_ICENABLER + (irq / 32) * 4) / 4] = BIT(irq % 32);
}

void gic_set_priority(int irq, u8 prio)
{
    volatile u8 *p;
    if (irq < 0 || irq >= IRQ_NR) {
        return;
    }
    p = (volatile u8 *)(T30_GIC_DIST_BASE + GICD_IPRIORITYR + irq);
    *p = prio;
}

void gic_set_type(int irq, int edge)
{
    u32 reg, shift, val;
    if (irq < 16 || irq >= IRQ_NR) {
        return;
    }
    reg = (GICD_ICFGR + (irq / 16) * 4) / 4;
    shift = ((irq % 16) * 2) + 1;
    val = GICD[reg];
    if (edge) {
        val |= BIT(shift);
    } else {
        val &= ~BIT(shift);
    }
    GICD[reg] = val;
}

int gic_pending(void)
{
    u32 irq = GICC[GICC_IAR / 4] & 0x3FFu;
    return (int)irq;
}

void gic_ack(int irq)
{
    (void)irq;
}

void gic_eoi(int irq)
{
    GICC[GICC_EOIR / 4] = (u32)irq;
}

void gic_dump(void)
{
    int i, shown = 0;
    printk("gic: enabled irqs:");
    for (i = 0; i < IRQ_NR; i++) {
        if (irq_table[i].fn) {
            printk(" %d", i);
            shown++;
        }
    }
    if (!shown) {
        printk(" (none)");
    }
    printk("\n");
}

int irq_register(int irq, irq_handler_fn fn, void *arg)
{
    if (irq < 0 || irq >= IRQ_NR) {
        return -1;
    }
    irq_table[irq].fn = fn;
    irq_table[irq].arg = arg;
    gic_set_priority(irq, 0x80);
    gic_enable_irq(irq);
    return 0;
}

void irq_unregister(int irq)
{
    if (irq < 0 || irq >= IRQ_NR) {
        return;
    }
    gic_disable_irq(irq);
    irq_table[irq].fn = 0;
    irq_table[irq].arg = 0;
}

void irq_dispatch(int irq)
{
    if (irq < 0 || irq >= IRQ_NR) {
        return;
    }
    if (irq_table[irq].fn) {
        irq_table[irq].count++;
        irq_table[irq].fn(irq, irq_table[irq].arg);
    }
}

u32 irq_save(void)
{
    return arm_irq_save();
}

void irq_restore(u32 flags)
{
    arm_irq_restore(flags);
}

void irq_disable_all(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
}

void irq_enable_all(void)
{
    __asm__ __volatile__("cpsie i" ::: "memory");
}

void spin_lock(spinlock_t *l)
{
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        while (l->locked) {
            __asm__ __volatile__("yield");
        }
    }
    __asm__ __volatile__("dmb" ::: "memory");
}

void spin_unlock(spinlock_t *l)
{
    __asm__ __volatile__("dmb" ::: "memory");
    __sync_lock_release(&l->locked);
}

u32 spin_lock_irqsave(spinlock_t *l)
{
    u32 f = arm_irq_save();
    spin_lock(l);
    return f;
}

void spin_unlock_irqrestore(spinlock_t *l, u32 flags)
{
    spin_unlock(l);
    arm_irq_restore(flags);
}
