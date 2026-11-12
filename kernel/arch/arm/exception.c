/* SurfaceRT_OS - exception and interrupt entry points (C side) */
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/string.h>
#include <srtos/sched.h>

#define SPURIOUS_IRQ 1023

void arm_handle_irq(u32 *regs)
{
    int irq;
    (void)regs;

    irq = gic_pending();
    if (irq == SPURIOUS_IRQ || irq < 0) {
        return;
    }
    irq_dispatch(irq);
    gic_eoi(irq);
}

void arm_handle_svc(u32 *regs)
{
    u32 num = regs[0]; /* r0 was pushed first */
    (void)num;
    /* No user space yet: an SVC from kernel context is a bug. */
    printk("svc: unexpected supervisor call (pc=0x%08x lr=0x%08x)\n",
           regs[13], regs[14]);
}

static void dump_frame(const char *what, u32 *regs, u32 status, u32 address)
{
    int i;
    printk("\n*** %s ***\n", what);
    printk("status=0x%08x address=0x%08x cpsr=0x%08x spsr=0x%08x\n",
           status, address, arm_read_cpsr(), arm_read_spsr());
    printk("pc=0x%08x lr=0x%08x sp=0x%08x\n", regs[15], regs[14],
           (u32)(uptr)regs);
    for (i = 0; i < 13; i += 4) {
        printk("r%-2d=0x%08x r%-2d=0x%08x r%-2d=0x%08x r%-2d=0x%08x\n",
               i, regs[i], i + 1, regs[i + 1], i + 2, regs[i + 2], i + 3,
               regs[i + 3]);
    }
}

void arm_handle_undef(u32 *regs, u32 status, u32 address)
{
    (void)status;
    (void)address;
    dump_frame("undefined instruction", regs, status, address);
}

void arm_handle_prefetch(u32 *regs, u32 status, u32 address)
{
    dump_frame("prefetch abort", regs, status, address);
}

void arm_handle_data_abort(u32 *regs, u32 status, u32 address)
{
    dump_frame("data abort", regs, status, address);
}

void arm_exception_panic(void)
{
    panic("unhandled exception - system halted");
}
