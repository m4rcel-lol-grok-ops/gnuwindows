/*
 * GWKernel CPU initialization - Phase 2 + 5
 */

#include <gw/cpu.h>
#include <gw/gdt.h>
#include <gw/idt.h>
#include <gw/irq.h>
#include <gw/timer.h>
#include <gw/serial.h>
#include <stdint.h>

void cpu_init(void) {
    serial_write("Initializing GDT...\n");
    gdt_init();
    serial_write("  GDT loaded (kernel code=0x08, data=0x10)\n");

    serial_write("Initializing IDT...\n");
    idt_init();
    serial_write("  IDT loaded (exceptions 0-31, IRQs 32-47)\n");

    serial_write("Initializing PIC...\n");
    irq_init();
    serial_write("  PIC remapped, all IRQs masked\n");

    serial_write("CPU initialization complete.\n");
}

void cpu_enable_interrupts(void) {
    __asm__ volatile ("sti");
}

void cpu_disable_interrupts(void) {
    __asm__ volatile ("cli");
}

/* Kept for compatibility; Phase 2 test can still be called if desired */
void cpu_test_exceptions(void) {
    serial_write("Exception test skipped in Phase 5 (timer demo active).\n");
}
