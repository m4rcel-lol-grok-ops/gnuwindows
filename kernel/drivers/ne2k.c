/*
 * NE2000 ISA (QEMU ne2k_isa) @ 0x300 — careful RX ring handling
 */

#include <gw/ne2k.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define NE2K_BASE 0x300
#define NE_CMD    (NE2K_BASE + 0x00)
#define NE_PSTART (NE2K_BASE + 0x01)
#define NE_PSTOP  (NE2K_BASE + 0x02)
#define NE_BNRY   (NE2K_BASE + 0x03)
#define NE_TPSR   (NE2K_BASE + 0x04)
#define NE_TBCR0  (NE2K_BASE + 0x05)
#define NE_TBCR1  (NE2K_BASE + 0x06)
#define NE_ISR    (NE2K_BASE + 0x07)
#define NE_RSAR0  (NE2K_BASE + 0x08)
#define NE_RSAR1  (NE2K_BASE + 0x09)
#define NE_RBCR0  (NE2K_BASE + 0x0A)
#define NE_RBCR1  (NE2K_BASE + 0x0B)
#define NE_RCR    (NE2K_BASE + 0x0C)
#define NE_TCR    (NE2K_BASE + 0x0D)
#define NE_DCR    (NE2K_BASE + 0x0E)
#define NE_IMR    (NE2K_BASE + 0x0F)
#define NE_DATA   (NE2K_BASE + 0x10)
#define NE_PAR0   (NE2K_BASE + 0x01)
#define NE_CURR   (NE2K_BASE + 0x07)

#define PG_TX    0x40
#define PG_START 0x46
#define PG_STOP  0x80

static uint8_t mac[6];
static int present;

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r;
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint16_t inw(uint16_t p) {
    uint16_t r; __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(p)); return r;
}

static void page0(void) { outb(NE_CMD, (uint8_t)((inb(NE_CMD) & 0x3F) | 0x00)); }
static void page1(void) { outb(NE_CMD, (uint8_t)((inb(NE_CMD) & 0x3F) | 0x40)); }

static void dma_done(void) {
    outb(NE_CMD, 0x22); /* page0 start, no DMA */
}

int ne2k_init(void) {
    present = 0;
    inb(NE2K_BASE + 0x1F);
    for (volatile int i = 0; i < 50000; i++) {}
    outb(NE2K_BASE + 0x1F, 0x00);

    outb(NE_CMD, 0x21);
    outb(NE_DCR, 0x49);
    outb(NE_RBCR0, 0);
    outb(NE_RBCR1, 0);
    outb(NE_RCR, 0x20);
    outb(NE_TCR, 0x02);
    outb(NE_PSTART, PG_START);
    outb(NE_PSTOP, PG_STOP);
    outb(NE_BNRY, PG_START);

    outb(NE_ISR, 0xFF);
    outb(NE_RSAR0, 0);
    outb(NE_RSAR1, 0);
    outb(NE_RBCR0, 32);
    outb(NE_RBCR1, 0);
    outb(NE_CMD, 0x0A);

    uint8_t prom[32];
    for (int i = 0; i < 16; i++) {
        uint16_t w = inw(NE_DATA);
        prom[i * 2] = (uint8_t)w;
        prom[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    dma_done();
    for (int i = 0; i < 6; i++)
        mac[i] = prom[i * 2];

    if (!(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) {
        serial_write("ne2k: no card\n");
        return -1;
    }

    page1();
    for (int i = 0; i < 6; i++)
        outb(NE_PAR0 + i, mac[i]);
    outb(NE_CURR, PG_START + 1);
    for (int i = 0; i < 8; i++)
        outb(NE2K_BASE + 0x08 + i, 0xFF);

    page0();
    outb(NE_ISR, 0xFF);
    outb(NE_IMR, 0x00);
    outb(NE_RCR, 0x04);
    outb(NE_TCR, 0x00);
    outb(NE_CMD, 0x22);

    present = 1;
    serial_write("ne2k: MAC ");
    for (int i = 0; i < 6; i++) {
        const char *h = "0123456789ABCDEF";
        serial_putc(h[mac[i] >> 4]);
        serial_putc(h[mac[i] & 0xF]);
        if (i < 5) serial_putc(':');
    }
    serial_write("\n");
    return 0;
}

const uint8_t *ne2k_mac(void) { return mac; }

int ne2k_send(const void *frame, size_t len) {
    if (!present || len < 60 || len > 1514) return -1;

    /* wait any prior remote DMA */
    for (int i = 0; i < 10000; i++) {
        if (!(inb(NE_CMD) & 0x04)) break;
    }

    page0();
    outb(NE_RBCR0, (uint8_t)(len & 0xFF));
    outb(NE_RBCR1, (uint8_t)(len >> 8));
    outb(NE_RSAR0, 0);
    outb(NE_RSAR1, PG_TX);
    outb(NE_CMD, 0x12);

    const uint16_t *w = frame;
    size_t words = (len + 1) / 2;
    for (size_t i = 0; i < words; i++)
        outw(NE_DATA, w[i]);

    outb(NE_TPSR, PG_TX);
    outb(NE_TBCR0, (uint8_t)(len & 0xFF));
    outb(NE_TBCR1, (uint8_t)(len >> 8));
    outb(NE_CMD, 0x26);

    for (int t = 0; t < 200000; t++) {
        uint8_t isr = inb(NE_ISR);
        if (isr & 0x02) {
            outb(NE_ISR, 0x02);
            return 0;
        }
        if (isr & 0x08) {
            outb(NE_ISR, 0x08);
            return -1;
        }
    }
    return -1;
}

static void ring_recover(void) {
    page1();
    uint8_t curr = inb(NE_CURR);
    page0();
    uint8_t b = (curr == PG_START) ? (uint8_t)(PG_STOP - 1) : (uint8_t)(curr - 1);
    outb(NE_BNRY, b);
    outb(NE_ISR, 0xFF);
    dma_done();
}

int ne2k_poll(void *buf, size_t maxlen) {
    if (!present) return 0;

    page0();
    uint8_t bnry = inb(NE_BNRY);
    page1();
    uint8_t curr = inb(NE_CURR);
    page0();

    if (bnry < PG_START || bnry >= PG_STOP)
        bnry = PG_START;
    if (curr < PG_START || curr >= PG_STOP) {
        ring_recover();
        return 0;
    }

    uint8_t next = (uint8_t)(bnry + 1);
    if (next >= PG_STOP)
        next = PG_START;
    if (next == curr)
        return 0;

    /* Read NIC packet header (4 bytes) at start of frame */
    outb(NE_RBCR0, 4);
    outb(NE_RBCR1, 0);
    outb(NE_RSAR0, 0);
    outb(NE_RSAR1, next);
    outb(NE_CMD, 0x0A);
    uint16_t h0 = inw(NE_DATA);
    uint16_t h1 = inw(NE_DATA);
    dma_done();

    uint8_t status = (uint8_t)(h0 & 0xFF);
    uint8_t next_pg = (uint8_t)(h0 >> 8);
    uint16_t count = h1;

    if (!(status & 0x01) || count < 60 || count > 1518 ||
        next_pg < PG_START || next_pg > PG_STOP) {
        ring_recover();
        return -1;
    }

    size_t payload = (size_t)count - 4; /* strip NIC header already accounted: count is frame length */
    /* count is length of received ethernet frame including FCS sometimes.
       Header is NOT part of count on some clones — OSDev: length includes header?
       Actually: "length of the packet" is the length field in the header = bytes following header.
       We'll use count as total packet buffer length including 4-byte header. */
    if (count <= 4) {
        ring_recover();
        return -1;
    }
    payload = (size_t)count - 4;
    if (payload > maxlen)
        payload = maxlen;

    outb(NE_RBCR0, (uint8_t)(payload & 0xFF));
    outb(NE_RBCR1, (uint8_t)(payload >> 8));
    outb(NE_RSAR0, 4);
    outb(NE_RSAR1, next);
    outb(NE_CMD, 0x0A);

    uint16_t *out = buf;
    size_t words = (payload + 1) / 2;
    for (size_t i = 0; i < words; i++)
        out[i] = inw(NE_DATA);
    dma_done();

    /* BNRY = next_pg - 1 (last page we finished) */
    uint8_t new_bnry;
    if (next_pg == PG_START)
        new_bnry = (uint8_t)(PG_STOP - 1);
    else
        new_bnry = (uint8_t)(next_pg - 1);
    outb(NE_BNRY, new_bnry);
    outb(NE_ISR, 0x01);
    return (int)payload;
}
