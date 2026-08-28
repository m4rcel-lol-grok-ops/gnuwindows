/*
 * 8259 PIC driver - Phase 5
 */

#include <gw/pic.h>
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void pic_init(void) {
    /* Start init sequence (cascade mode) */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    /* Vector offsets: master 32, slave 40 */
    outb(PIC1_DATA, 32);
    outb(PIC2_DATA, 40);

    /* Tell master about slave at IRQ2, slave its cascade identity */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* 8086 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Mask all IRQs initially */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    uint8_t val = inb(port) | (uint8_t)(1u << irq);
    outb(port, val);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    uint8_t val = inb(port) & (uint8_t)~(1u << irq);
    outb(port, val);
}
