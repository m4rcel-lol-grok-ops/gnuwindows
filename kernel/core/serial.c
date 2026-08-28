#include <gw/serial.h>
#include <gw/vga.h>
#include <gw/fb.h>
#include <stddef.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void serial_init(void) {
    vga_init();
    outb(COM1 + 1, 0x00); /* disable COM interrupts for polling */
    outb(COM1 + 3, 0x80); /* DLAB */
    outb(COM1 + 0, 0x01); /* 115200 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO */
    outb(COM1 + 4, 0x0B); /* MCR */
}

void serial_putc(char c) {
    vga_putc(c);
    fb_putc(c);
    int spins = 0;
    while (!(inb(COM1 + 5) & 0x20) && spins++ < 100000)
        ;
    if (c == '\n') {
        outb(COM1, '\r');
        spins = 0;
        while (!(inb(COM1 + 5) & 0x20) && spins++ < 100000)
            ;
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    if (!s) return;
    while (*s) serial_putc(*s++);
}

void serial_write_hex(uint64_t val) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 15; i >= 0; i--)
        serial_putc(hex[(val >> (i * 4)) & 0xF]);
}

void serial_write_dec(uint64_t val) {
    char buf[24];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i < 23) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
}

int serial_rx_ready(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

int serial_getc_nonblock(void) {
    if (!serial_rx_ready())
        return -1;
    return (int)inb(COM1);
}

char serial_getc(void) {
    for (;;) {
        int c = serial_getc_nonblock();
        if (c >= 0)
            return (char)c;
        __asm__ volatile ("pause");
    }
}
