/*
 * Programmable Interval Timer (8253/8254) - Phase 5
 */

#include <gw/pit.h>
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t divisor = PIT_FREQ / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Channel 0, lo/hi access, mode 3 (square wave), binary */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}
