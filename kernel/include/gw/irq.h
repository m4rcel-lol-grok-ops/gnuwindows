#ifndef GW_IRQ_H
#define GW_IRQ_H

#include <stdint.h>

/* Install IRQ stubs into IDT vectors 32-47 and init PIC */
void irq_init(void);

#endif
