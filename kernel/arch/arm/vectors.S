/*
 * SurfaceRT_OS - ARMv7 exception vectors
 *
 * The vector table is copied/kept in .text.vectors and installed via VBAR.
 * IRQ/FIQ/abort entry code saves a full frame on the *current* (SVC) stack so
 * that the scheduler can switch threads from inside the interrupt handler.
 */

    .syntax unified
    .arm
    .section .text.vectors, "ax"
    .global _vectors
    .balign 32
_vectors:
    b   _reset_handler
    b   _undef_handler
    b   _svc_handler
    b   _prefetch_handler
    b   _data_abort_handler
    b   _reserved_handler
    b   _irq_handler
    b   _fiq_handler

_reset_handler:
    b   _reset_handler

_undef_handler:
    sub     lr, lr, #4
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    mrc     p15, 0, r1, c5, c0, 0       /* DFSR  */
    mrc     p15, 0, r2, c6, c0, 0       /* DFAR  */
    bl      arm_handle_undef
    b       _panic_exit

_svc_handler:
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    bl      arm_handle_svc
    pop     {r0-r12, lr}
    rfeia   sp!

_prefetch_handler:
    sub     lr, lr, #4
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    mrc     p15, 0, r1, c5, c0, 0       /* IFSR  */
    mrc     p15, 0, r2, c6, c0, 2       /* IFAR  */
    bl      arm_handle_prefetch
    b       _panic_exit

_data_abort_handler:
    sub     lr, lr, #8
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    mrc     p15, 0, r1, c5, c0, 0       /* DFSR  */
    mrc     p15, 0, r2, c6, c0, 0       /* DFAR  */
    bl      arm_handle_data_abort
    b       _panic_exit

_reserved_handler:
    b       _reserved_handler

_irq_handler:
    sub     lr, lr, #4
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    bl      arm_handle_irq
    pop     {r0-r12, lr}
    rfeia   sp!

_fiq_handler:
    b       _fiq_handler

_panic_exit:
    bl      arm_exception_panic
1:  wfi
    b       1b

    .section .text
    .global arm_vectors_install
    .type arm_vectors_install, %function
/* void arm_vectors_install(void) - point VBAR at our table */
arm_vectors_install:
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0
    dsb
    isb
    bx      lr
    .size arm_vectors_install, . - arm_vectors_install
