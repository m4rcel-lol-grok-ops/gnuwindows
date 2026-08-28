/*
 * GWKernel x86_64 entry point
 * Called by the UEFI bootloader after ExitBootServices.
 * Microsoft x64 ABI (RCX = first arg) is converted to System V (RDI).
 */

.section .text.boot
.global _start
.extern kmain

_start:
    cli

    /* Microsoft x64 ABI (UEFI) -> System V: first arg RCX -> RDI */
    mov %rcx, %rdi

    /* Early stack in low identity-mapped memory */
    mov $0x90000, %rsp
    and $~0xF, %rsp

    cld
    call kmain

.hang:
    cli
    hlt
    jmp .hang
