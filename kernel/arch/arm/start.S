/*
 * SurfaceRT_OS - ARMv7-A kernel entry (Cortex-A9 on T30, Cortex-A15 on T114)
 *
 * Entered from the UEFI loader with:
 *   r0 = physical address of struct srt_bootinfo
 *   r1 = SRT_BOOT_MAGIC
 *   MMU on, identity mapped, at the load address chosen by the loader (the
 *   image has already been relocated for it), usually in SVC mode - but Tegra 4
 *   firmware may hand over at EL2, so the entry code normalises that first.
 *
 * We drop the firmware's MMU/cache state, install our own tables and continue
 * in C.
 */

    .syntax unified
    .arch_extension virt
    .arm
    .section .text.start, "ax"
    .global _start
    .type _start, %function
_start:
    /* r0 = bootinfo, r1 = magic - stash them in r11/r10 across early init */
    mov     r11, r0
    mov     r10, r1

    /* mask all interrupts */
    cpsid   aif

    /*
     * Some Tegra 4 (Cortex-A15) firmware hands over at EL2 with the hypervisor
     * enabled.  We are a single-OS kernel, so turn the hypervisor into a
     * pass-through and drop to EL1:
     *   HCR.VM   = 0  no stage-2 translation (our MMIO must not fault)
     *   HCR.IMO/FMO/AMO = 0  physical interrupts stay at EL1
     *   CNTHCTL  = EL1PCTEN|EL1PCEN  EL1 may use the physical timer
     * On the Tegra 3 (Cortex-A9, SVC from the firmware) this whole block is
     * skipped, and the instructions are never executed.
     */
    mrs     r0, cpsr
    and     r0, r0, #0x1f
    cmp     r0, #0x1a                /* HYP mode? */
    bne     1f

    mrc     p15, 4, r0, c1, c1, 0    /* HCR */
    bic     r0, r0, #(1 << 0)        /* VM  */
    bic     r0, r0, #(1 << 3)        /* IMO */
    bic     r0, r0, #(1 << 4)        /* FMO */
    bic     r0, r0, #(1 << 5)        /* AMO */
    mcr     p15, 4, r0, c1, c1, 0
    mov     r0, #3                   /* CNTHCTL: EL1PCTEN | EL1PCEN */
    mcr     p15, 4, r0, c14, c1, 0
    isb

    adr     r0, 1f                   /* resume at EL1, right here */
    msr     elr_hyp, r0
    mrs     r0, cpsr
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13            /* SVC = EL1 */
    msr     spsr_hyp, r0
    eret

1:
    /* enter SVC mode with interrupts masked */
    mrs     r0, cpsr
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13            /* SVC */
    orr     r0, r0, #0xc0            /* IRQ+FIQ disabled */
    msr     cpsr_c, r0

    /* invalidate I-cache, D-cache, branch predictor, TLBs */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0    /* ICIALLU */
    mcr     p15, 0, r0, c7, c5, 6    /* BPIALL  */
    mcr     p15, 0, r0, c8, c7, 0    /* TLBIALL */
    dsb
    isb

    /* disable MMU, D-cache, I-cache, branch prediction */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 0)        /* MMU          */
    bic     r0, r0, #(1 << 2)        /* D-cache      */
    bic     r0, r0, #(1 << 12)       /* I-cache      */
    bic     r0, r0, #(1 << 11)       /* branch pred  */
    mcr     p15, 0, r0, c1, c0, 0
    dsb
    isb

    /* set up the SVC stack */
    ldr     sp, =__stack_top

    /* clear .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    /* give the C world its boot arguments */
    mov     r0, r11
    mov     r1, r10
    bl      arm_early_init

    /* should not return */
2:  wfi
    b       2b
    .size _start, . - _start

/* 16 KiB of boot stack */
    .section .bss
    .balign 16
    .global __stack_bottom
__stack_bottom:
    .space 16384
    .global __stack_top
__stack_top:

    .section .text
    .global arm_cpu_id
    .type arm_cpu_id, %function
arm_cpu_id:
    mrc     p15, 0, r0, c0, c0, 5    /* MPIDR */
    and     r0, r0, #0xff
    bx      lr
    .size arm_cpu_id, . - arm_cpu_id
