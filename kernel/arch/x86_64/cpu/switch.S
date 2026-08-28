/*
 * Context switch - Phase 6
 *
 * void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
 *   Saves callee-saved regs + return address context onto *old_rsp stack,
 *   switches to new_rsp, restores regs, returns into the new thread.
 *
 * System V: RDI = old_rsp ptr, RSI = new_rsp value
 */

.section .text
.global context_switch
.global thread_root_return

context_switch:
    /* Save callee-saved registers of current thread */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Store current RSP into *old_rsp */
    movq %rsp, (%rdi)

    /* Load new thread stack */
    movq %rsi, %rsp

    /* Restore callee-saved of next thread */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    ret
    /* ret pops the return address placed when the thread was created
       or the resume point from a previous switch */

/* First entry for a new thread: stack is set up so ret lands in thread_trampoline */
.global thread_trampoline
thread_trampoline:
    sti
    /* RDI still has arg if we set it up; we pass arg via stack slot instead */
    popq %rdi           /* arg */
    popq %rax           /* entry function */
    call *%rax
    /* If entry returns, kill the thread */
    call thread_exit
    /* never returns */
1:  hlt
    jmp 1b
