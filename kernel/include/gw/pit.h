#ifndef GW_PIT_H
#define GW_PIT_H

#include <stdint.h>

#define PIT_CH0    0x40
#define PIT_CMD    0x43
#define PIT_FREQ   1193182u

/* Program channel 0 for periodic interrupts at approx `hz` */
void pit_init(uint32_t hz);

#endif
