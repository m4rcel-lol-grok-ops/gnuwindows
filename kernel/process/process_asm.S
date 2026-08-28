.section .text
.global process_switch_to_user
.global process_user_resume

/*
 * void process_switch_to_user(uint64_t entry, uint64_t user_rsp, uint64_t *saved_krsp)
 * RDI=entry RSI=user_rsp RDX=saved_krsp
 */
process_switch_to_user:
    mov %rsp, (%rdx)          /* save kernel RSP for SYS_EXIT return */
    cli
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    pushq $0x23               /* SS */
    pushq %rsi                /* user RSP */
    pushq $0x202              /* RFLAGS */
    pushq $0x1B               /* CS */
    pushq %rdi                /* RIP = entry */
    iretq

process_user_resume:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    /* RSP already set by iretq from exit handler */
    ret
