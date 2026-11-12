/*
 * SurfaceRT_OS - thread context switch
 *
 *   void arm_switch_context(u32 **save_sp, u32 *new_sp);
 *
 * Callee-saved registers plus LR are pushed on the outgoing stack, the stack
 * pointer is published, and the incoming frame is popped.  Called from
 * schedule() with interrupts disabled.
 */
    .syntax unified
    .arm
    .text
    .global arm_switch_context
    .type arm_switch_context, %function
arm_switch_context:
    push    {r4-r11, lr}
    str     sp, [r0]
    mov     sp, r1
    pop     {r4-r11, lr}
    bx      lr
    .size arm_switch_context, . - arm_switch_context

/*
 * void arm_thread_trampoline(void)
 * Every new thread starts here; r0 holds the entry function pointer and r1 the
 * argument (placed by thread_create()).  It must never return.
 */
    .global arm_thread_trampoline
    .type arm_thread_trampoline, %function
arm_thread_trampoline:
    pop     {r0, r1}
    cpsie   i                /* schedule() runs with IRQs masked: a brand new
                              * thread has no saved CPSR to restore, so enable
                              * them here before entering the thread body */
    blx     r0
    bl      thread_exit_trampoline
1:  b       1b
    .size arm_thread_trampoline, . - arm_thread_trampoline
