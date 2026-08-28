/*
 * Standalone user program for GWKernel (ELF + embedded blob)
 */
.section .text

.global user_program_start
.global user_program_end

user_program_start:

    lea msg(%rip), %rsi
    mov $msg_end - msg, %rdx
    mov $1, %rdi
    mov $2, %rax
    int $0x80

    mov $6, %rax
    int $0x80

    lea bye(%rip), %rsi
    mov $bye_end - bye, %rdx
    mov $1, %rdi
    mov $2, %rax
    int $0x80

    xor %rdi, %rdi
    mov $1, %rax
    int $0x80
1:  jmp 1b

msg:
    .ascii "[ELF] Hello from userspace (exec)\n"
msg_end:
bye:
    .ascii "[ELF] SYS_EXIT\n"
bye_end:

user_program_end:
