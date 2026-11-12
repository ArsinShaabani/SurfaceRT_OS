/*
 * SurfaceRT_OS - ARMv7-A (Cortex-A9) kernel entry
 *
 * Entered from the UEFI loader with:
 *   r0 = physical address of struct srt_bootinfo
 *   r1 = SRT_BOOT_MAGIC
 *   MMU on, identity mapped, running in SVC mode at the load address chosen by
 *   the loader (the image has already been relocated for that address).
 *
 * We drop the firmware's MMU/cache state, install our own tables and continue
 * in C.
 */

    .syntax unified
    .arm
    .section .text.start, "ax"
    .global _start
    .type _start, %function
_start:
    /* r0 = bootinfo, r1 = magic - stash them in r11/r10 across early init */
    mov     r11, r0
    mov     r10, r1

    /* mask all interrupts, enter SVC mode */
    cpsid   aif
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
