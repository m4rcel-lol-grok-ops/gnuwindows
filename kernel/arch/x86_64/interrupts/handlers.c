/*
 * GWKernel exception / interrupt handlers - Phase 2
 */

#include <gw/serial.h>
#include <stdint.h>

/* Layout matches the pushes in isr.S (reverse order of push) */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static const char *exception_names[32] = {
    "Divide Error (#DE)",
    "Debug (#DB)",
    "NMI",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 FPU Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point (#XM)",
    "Virtualization (#VE)",
    "Control Protection (#CP)",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception (#SX)",
    "Reserved"
};

void isr_common_handler(struct interrupt_frame *frame) {
    uint64_t vec = frame->vector;

    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  GWKernel Exception\n");
    serial_write("========================================\n");

    if (vec < 32) {
        serial_write("Type: ");
        serial_write(exception_names[vec]);
        serial_write("\n");
    } else {
        serial_write("Type: Unknown vector ");
        serial_write_dec(vec);
        serial_write("\n");
    }

    serial_write("Vector: ");
    serial_write_dec(vec);
    serial_write("\n");

    serial_write("Error Code: ");
    serial_write_hex(frame->error_code);
    serial_write("\n");

    serial_write("Instruction Pointer: ");
    serial_write_hex(frame->rip);
    serial_write("\n");

    serial_write("CS: ");
    serial_write_hex(frame->cs);
    serial_write("  RFLAGS: ");
    serial_write_hex(frame->rflags);
    serial_write("\n");

    serial_write("RSP: ");
    serial_write_hex(frame->rsp);
    serial_write("  SS: ");
    serial_write_hex(frame->ss);
    serial_write("\n");

    serial_write("RAX: ");
    serial_write_hex(frame->rax);
    serial_write("  RBX: ");
    serial_write_hex(frame->rbx);
    serial_write("\n");
    serial_write("RCX: ");
    serial_write_hex(frame->rcx);
    serial_write("  RDX: ");
    serial_write_hex(frame->rdx);
    serial_write("\n");

    if (vec == 14) { /* Page Fault - CR2 holds faulting address */
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_write("CR2 (fault addr): ");
        serial_write_hex(cr2);
        serial_write("\n");
        serial_write("Error bits: ");
        if (frame->error_code & 1) serial_write("P ");
        else serial_write("!P ");
        if (frame->error_code & 2) serial_write("W ");
        else serial_write("R ");
        if (frame->error_code & 4) serial_write("U ");
        else serial_write("S ");
        if (frame->error_code & 8) serial_write("RSVD ");
        if (frame->error_code & 16) serial_write("I/D ");
        serial_write("\n");
    }

    serial_write("Process: kernel\n");
    serial_write("========================================\n");
    serial_write("System halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
