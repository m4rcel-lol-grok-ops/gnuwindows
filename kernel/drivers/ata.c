/*
 * ATA PIO LBA28 — primary master, read + write
 */

#include <gw/ata.h>
#include <gw/serial.h>
#include <stdint.h>

#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_CMD         0x1F7
#define ATA_ALT_STATUS  0x3F6

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r;
}
static inline uint16_t inw(uint16_t p) {
    uint16_t r; __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(p)); return r;
}

static int ata_present;

static void io_wait(void) {
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
}

static int wait_bsy_clear(void) {
    for (int i = 0; i < 5000000; i++)
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    return -1;
}

static int wait_drq(void) {
    for (int i = 0; i < 5000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

int ata_init(void) {
    outb(ATA_DRIVE, 0xE0);
    io_wait();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, ATA_CMD_IDENTIFY);
    io_wait();
    if (inb(ATA_STATUS) == 0 || inb(ATA_STATUS) == 0xFF) {
        serial_write("ATA: no disk\n");
        return -1;
    }
    if (wait_bsy_clear() || wait_drq()) return -1;
    for (int i = 0; i < 256; i++) (void)inw(ATA_DATA);
    ata_present = 1;
    serial_write("ATA: primary master ready (PIO LBA28 R/W)\n");
    return 0;
}

static void setup_lba(uint32_t lba) {
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!ata_present || !count) return -1;
    uint16_t *out = buf;
    for (uint32_t n = 0; n < count; n++) {
        if (wait_bsy_clear()) return -1;
        setup_lba(lba + n);
        outb(ATA_CMD, ATA_CMD_READ_PIO);
        io_wait();
        if (wait_drq()) return -1;
        for (int i = 0; i < 256; i++) *out++ = inw(ATA_DATA);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!ata_present || !count) return -1;
    const uint16_t *in = buf;
    for (uint32_t n = 0; n < count; n++) {
        if (wait_bsy_clear()) return -1;
        setup_lba(lba + n);
        outb(ATA_CMD, ATA_CMD_WRITE_PIO);
        io_wait();
        if (wait_drq()) return -1;
        for (int i = 0; i < 256; i++) outw(ATA_DATA, in[i]);
        in += 256;
        /* After data: device processes write — poll BSY via ALT (don't clear IRQ) */
        for (int i = 0; i < 5000000; i++) {
            if (!(inb(ATA_ALT_STATUS) & ATA_SR_BSY)) break;
        }
        uint8_t s = inb(ATA_STATUS); /* clear INTRQ */
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
    }
    return 0;
}

void ata_flush(void) {
    if (!ata_present) return;
    if (wait_bsy_clear()) return;
    outb(ATA_CMD, ATA_CMD_CACHE_FLUSH);
    io_wait();
    wait_bsy_clear();
}
