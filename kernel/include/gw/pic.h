#ifndef GW_PIC_H
#define GW_PIC_H

#include <stdint.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI    0x20

/* Remap IRQs to IDT vectors 32-47 and mask all initially */
void pic_init(void);

/* Send End-Of-Interrupt */
void pic_eoi(uint8_t irq);

/* Mask / unmask a single IRQ (0-15) */
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

#endif
