/*
 * GWKernel IDT - Phase 2 + Phase 5 (IRQ vectors)
 */

#include <gw/idt.h>
#include <gw/gdt.h>
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtr;

extern void isr_stub_0(void);  extern void isr_stub_1(void);
extern void isr_stub_2(void);  extern void isr_stub_3(void);
extern void isr_stub_4(void);  extern void isr_stub_5(void);
extern void isr_stub_6(void);  extern void isr_stub_7(void);
extern void isr_stub_8(void);  extern void isr_stub_9(void);
extern void isr_stub_10(void); extern void isr_stub_11(void);
extern void isr_stub_12(void); extern void isr_stub_13(void);
extern void isr_stub_14(void); extern void isr_stub_15(void);
extern void isr_stub_16(void); extern void isr_stub_17(void);
extern void isr_stub_18(void); extern void isr_stub_19(void);
extern void isr_stub_20(void); extern void isr_stub_21(void);
extern void isr_stub_22(void); extern void isr_stub_23(void);
extern void isr_stub_24(void); extern void isr_stub_25(void);
extern void isr_stub_26(void); extern void isr_stub_27(void);
extern void isr_stub_28(void); extern void isr_stub_29(void);
extern void isr_stub_30(void); extern void isr_stub_31(void);

extern void isr_stub_128(void);
extern void irq_stub_0(void);  extern void irq_stub_1(void);
extern void irq_stub_2(void);  extern void irq_stub_3(void);
extern void irq_stub_4(void);  extern void irq_stub_5(void);
extern void irq_stub_6(void);  extern void irq_stub_7(void);
extern void irq_stub_8(void);  extern void irq_stub_9(void);
extern void irq_stub_10(void); extern void irq_stub_11(void);
extern void irq_stub_12(void); extern void irq_stub_13(void);
extern void irq_stub_14(void); extern void irq_stub_15(void);

static void (*const isr_stubs[32])(void) = {
    isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
    isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
    isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
    isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
    isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
};

static void (*const irq_stubs[16])(void) = {
    irq_stub_0,  irq_stub_1,  irq_stub_2,  irq_stub_3,
    irq_stub_4,  irq_stub_5,  irq_stub_6,  irq_stub_7,
    irq_stub_8,  irq_stub_9,  irq_stub_10, irq_stub_11,
    irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
};

static void idt_set_gate(int vec, void (*handler)(void), uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = GDT_KERNEL_CODE;
    idt[vec].ist         = 0;
    idt[vec].type_attr   = type_attr;
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].zero       = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].zero = 0;
    }

    /* Exceptions 0-31 */
    for (int i = 0; i < 32; i++)
        idt_set_gate(i, isr_stubs[i], 0x8E);

    /* Hardware IRQs 32-47 */
    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, irq_stubs[i], 0x8E);

    /* Syscall gate: DPL=3 so ring3 can invoke later */
    idt_set_gate(128, isr_stub_128, 0xEE);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}
