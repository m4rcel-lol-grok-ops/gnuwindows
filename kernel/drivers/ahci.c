/*
 * Minimal AHCI (SATA) driver — first implemented port, DMA read/write.
 * For modern hardware where legacy IDE ports are absent.
 */

#include <gw/ahci.h>
#include <gw/pci.h>
#include <gw/pmm.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define HBA_GHC         0x04
#define HBA_PI          0x0C
#define HBA_PORT_OFFSET 0x100
#define HBA_PORT_SIZE   0x80

#define PX_CLB   0x00
#define PX_FB    0x08
#define PX_IS    0x10
#define PX_IE    0x14
#define PX_CMD   0x18
#define PX_TFD   0x20
#define PX_SIG   0x24
#define PX_SSTS  0x28
#define PX_SCTL  0x2C
#define PX_SERR  0x30
#define PX_CI    0x38

#define CMD_ST   (1u << 0)
#define CMD_FRE  (1u << 4)
#define CMD_FR   (1u << 14)
#define CMD_CR   (1u << 15)

#define SATA_SIG_ATA 0x00000101

static volatile uint8_t *abar;
static int port_num = -1;
static int present;
static uint8_t *cl_base;   /* command list 1K */
static uint8_t *fis_base;  /* received FIS 256 */
static uint8_t *ct_base;   /* command table */

static inline uint32_t rr(volatile uint8_t *b, uint32_t off) {
    return *(volatile uint32_t *)(b + off);
}
static inline void wr(volatile uint8_t *b, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(b + off) = v;
}

static volatile uint8_t *port_regs(int p) {
    return abar + HBA_PORT_OFFSET + (uint32_t)p * HBA_PORT_SIZE;
}

static void stop_cmd(volatile uint8_t *p) {
    uint32_t cmd = rr(p, PX_CMD);
    cmd &= ~CMD_ST;
    cmd &= ~CMD_FRE;
    wr(p, PX_CMD, cmd);
    for (int i = 0; i < 100000; i++) {
        cmd = rr(p, PX_CMD);
        if (!(cmd & CMD_FR) && !(cmd & CMD_CR)) break;
    }
}

static void start_cmd(volatile uint8_t *p) {
    uint32_t cmd;
    for (int i = 0; i < 100000; i++) {
        if (!(rr(p, PX_CMD) & CMD_CR)) break;
    }
    cmd = rr(p, PX_CMD);
    wr(p, PX_CMD, cmd | CMD_FRE);
    wr(p, PX_CMD, rr(p, PX_CMD) | CMD_ST);
}

static int find_ahci_bar(uint64_t *bar_out) {
    for (int bus = 0; bus < 4; bus++)
    for (int slot = 0; slot < 32; slot++)
    for (int fn = 0; fn < 8; fn++) {
        uint16_t vend = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0);
        if (vend == 0xFFFF) { if (fn == 0) break; continue; }
        uint8_t classc = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0B);
        uint8_t sub = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0A);
        uint8_t prog = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x09);
        if (classc == 0x01 && sub == 0x06 && prog == 0x01) {
            uint32_t bar = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x24);
            /* enable bus master + mem */
            uint16_t cmd = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x04);
            pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x04); /* keep */
            /* write command via CF8 - use 32-bit read-modify on 0x04 */
            {
                uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)(slot & 31) << 11)
                    | ((uint32_t)(fn & 7) << 8) | 0x04;
                __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
                uint32_t v;
                __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC));
                v |= 0x06; /* memory + bus master */
                __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
                __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"((uint16_t)0xCFC));
            }
            (void)cmd;
            if (bar & 4) {
                /* 64-bit BAR */
                uint32_t bar_hi = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x28);
                *bar_out = ((uint64_t)bar_hi << 32) | (bar & ~0xFULL);
            } else {
                *bar_out = bar & ~0xFULL;
            }
            serial_write("ahci: PCI ");
            serial_write_dec((uint64_t)bus);
            serial_putc(':');
            serial_write_dec((uint64_t)slot);
            serial_write(" ABAR=");
            serial_write_hex(*bar_out);
            serial_write("\n");
            return 0;
        }
        if (fn == 0) {
            uint8_t hdr = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0E);
            if (!(hdr & 0x80)) break;
        }
    }
    return -1;
}

int ahci_init(void) {
    present = 0;
    port_num = -1;
    uint64_t bar = 0;
    if (find_ahci_bar(&bar) != 0) {
        serial_write("ahci: no AHCI controller\n");
        return -1;
    }
    extern int vmm_map_physical_range(uint64_t, uint64_t);
    if (vmm_map_physical_range(bar, 0x1100) != 0) {
        serial_write("ahci: map ABAR failed\n");
        return -1;
    }
    abar = (volatile uint8_t *)(uintptr_t)bar;

    /* GHC.AE */
    wr(abar, HBA_GHC, rr(abar, HBA_GHC) | (1u << 31));

    uint32_t pi = rr(abar, HBA_PI);
    for (int p = 0; p < 32; p++) {
        if (!(pi & (1u << p))) continue;
        volatile uint8_t *pr = port_regs(p);
        uint32_t ssts = rr(pr, PX_SSTS);
        uint32_t det = ssts & 0xF;
        uint32_t ipm = (ssts >> 8) & 0xF;
        if (det != 3 || ipm != 1) continue;
        uint32_t sig = rr(pr, PX_SIG);
        if (sig != SATA_SIG_ATA) continue;

        stop_cmd(pr);
        /* allocate command structures (contiguous pages) */
        uint64_t page = pmm_alloc_pages(2);
        if (!page) return -1;
        cl_base = (uint8_t *)(uintptr_t)page;
        fis_base = cl_base + 1024;
        ct_base = cl_base + 2048;
        for (int i = 0; i < 4096; i++) cl_base[i] = 0;

        wr(pr, PX_CLB, (uint32_t)(uintptr_t)cl_base);
        wr(pr, PX_CLB + 4, (uint32_t)((uintptr_t)cl_base >> 32));
        wr(pr, PX_FB, (uint32_t)(uintptr_t)fis_base);
        wr(pr, PX_FB + 4, (uint32_t)((uintptr_t)fis_base >> 32));
        wr(pr, PX_IS, 0xFFFFFFFF);
        wr(pr, PX_SERR, 0xFFFFFFFF);
        start_cmd(pr);

        port_num = p;
        present = 1;
        serial_write("ahci: port ");
        serial_write_dec((uint64_t)p);
        serial_write(" ATA disk ready\n");
        return 0;
    }
    serial_write("ahci: no active ATA port\n");
    return -1;
}

int ahci_present(void) { return present; }

static int ahci_rw(uint64_t lba, uint32_t count, void *buf, int write) {
    if (!present || count == 0 || count > 8) return -1;
    volatile uint8_t *pr = port_regs(port_num);

    /* wait not busy */
    for (int i = 0; i < 1000000; i++) {
        if (!(rr(pr, PX_TFD) & 0x88)) break;
    }

    /* command header */
    uint32_t *ch = (uint32_t *)cl_base;
    for (int i = 0; i < 8; i++) ch[i] = 0;
    ch[0] = (uint32_t)((5 << 16) | (write ? (1u << 6) : 0) | (1u << 0)); /* CFL=5, W, PRDTL=1 */
    ch[1] = 0; /* PRDBC */
    ch[2] = (uint32_t)(uintptr_t)ct_base;
    ch[3] = (uint32_t)((uintptr_t)ct_base >> 32);

    uint8_t *ct = ct_base;
    for (int i = 0; i < 256; i++) ct[i] = 0;
    /* Register Host-to-Device FIS (type 0x27) */
    ct[0] = 0x27;
    ct[1] = 0x80; /* C=1: update command register */
    ct[2] = write ? 0x35 : 0x25; /* WRITE/READ DMA EXT */
    ct[3] = 0; /* features */
    ct[4] = (uint8_t)(lba);
    ct[5] = (uint8_t)(lba >> 8);
    ct[6] = (uint8_t)(lba >> 16);
    ct[7] = 0x40; /* device: LBA */
    ct[8] = (uint8_t)(lba >> 24);
    ct[9] = (uint8_t)(lba >> 32);
    ct[10] = (uint8_t)(lba >> 40);
    ct[11] = 0; /* features exp */
    ct[12] = (uint8_t)count;
    ct[13] = (uint8_t)(count >> 8);
    ct[14] = 0;
    ct[15] = 0; /* control */

    /* PRDT entry at offset 0x80 */
    uint32_t *prd = (uint32_t *)(ct + 0x80);
    uint64_t baddr = (uint64_t)(uintptr_t)buf;
    prd[0] = (uint32_t)baddr;
    prd[1] = (uint32_t)(baddr >> 32);
    prd[2] = 0;
    prd[3] = ((count * 512) - 1) | (1u << 31); /* DBC + I */

    wr(pr, PX_IS, 0xFFFFFFFF);
    wr(pr, PX_CI, 1);

    for (int i = 0; i < 2000000; i++) {
        if (!(rr(pr, PX_CI) & 1)) break;
        if (rr(pr, PX_IS) & (1u << 30)) {
            serial_write("ahci: task file error\n");
            return -1;
        }
    }
    if (rr(pr, PX_CI) & 1) return -1;
    return 0;
}

int ahci_read(uint64_t lba, uint32_t count, void *buf) {
    return ahci_rw(lba, count, buf, 0);
}

int ahci_write(uint64_t lba, uint32_t count, const void *buf) {
    return ahci_rw(lba, count, (void *)buf, 1);
}
