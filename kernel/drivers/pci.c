/*
 * PCI configuration space via CF8/CFC (type 1)
 */

#include <gw/pci.h>
#include <gw/serial.h>
#include <stdint.h>

static inline void outl(uint16_t p, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint32_t inl(uint16_t p) {
    uint32_t r; __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(p)); return r;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (uint32_t)((1u << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)(slot & 0x1F) << 11) |
        ((uint32_t)(func & 7) << 8) |
        (offset & 0xFC));
    outl(0xCF8, addr);
    return inl(0xCFC);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, (uint8_t)(offset & 0xFC));
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, (uint8_t)(offset & 0xFC));
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

static void print_hex16(uint16_t v) {
    const char *h = "0123456789ABCDEF";
    serial_putc(h[(v >> 12) & 0xF]);
    serial_putc(h[(v >> 8) & 0xF]);
    serial_putc(h[(v >> 4) & 0xF]);
    serial_putc(h[v & 0xF]);
}

static void print_hex8(uint8_t v) {
    const char *h = "0123456789ABCDEF";
    serial_putc(h[(v >> 4) & 0xF]);
    serial_putc(h[v & 0xF]);
}

void pci_list(void) {
    int found = 0;
    serial_write("PCI devices:\n");
    for (int bus = 0; bus < 4; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vend = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                if (vend == 0xFFFF) {
                    if (func == 0) break; /* no device in slot */
                    continue;
                }
                uint16_t dev = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x02);
                uint8_t classc = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0B);
                uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0A);
                uint8_t prog = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x09);
                uint8_t hdr = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x0E);

                serial_write("  ");
                print_hex8((uint8_t)bus);
                serial_putc(':');
                print_hex8((uint8_t)slot);
                serial_putc('.');
                serial_putc('0' + (func & 7));
                serial_write("  ");
                print_hex16(vend);
                serial_putc(':');
                print_hex16(dev);
                serial_write("  class ");
                print_hex8(classc);
                serial_putc('/');
                print_hex8(subclass);
                serial_putc('/');
                print_hex8(prog);
                if (classc == 0x01) serial_write("  [storage]");
                else if (classc == 0x02) serial_write("  [network]");
                else if (classc == 0x03) serial_write("  [display]");
                else if (classc == 0x06) serial_write("  [bridge]");
                else if (classc == 0x0C) serial_write("  [serial-bus]");
                serial_write("\n");
                found++;

                /* single-function device */
                if (func == 0 && !(hdr & 0x80))
                    break;
            }
        }
    }
    serial_write("pci: ");
    serial_write_dec((uint64_t)found);
    serial_write(" function(s)\n");
}

void pci_init(void) {
    /* Probe host bridge at 0:0.0 */
    uint16_t vend = pci_read16(0, 0, 0, 0x00);
    if (vend == 0xFFFF)
        serial_write("pci: no host bridge at 0:0.0\n");
    else {
        serial_write("pci: config OK (host vend=");
        print_hex16(vend);
        serial_write(")\n");
    }
}
