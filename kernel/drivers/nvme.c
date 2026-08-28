/*
 * Minimal NVMe driver — NSID 1, admin + IO queue pair, 512-byte LBA R/W.
 */

#include <gw/nvme.h>
#include <gw/pci.h>
#include <gw/pmm.h>
#include <gw/vmm.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define NVME_CAP  0x00
#define NVME_CC   0x14
#define NVME_CSTS 0x1C
#define NVME_AQA  0x24
#define NVME_ASQ  0x28
#define NVME_ACQ  0x30

#define CC_EN    (1u << 0)
#define CSTS_RDY (1u << 0)

#define QDEPTH 16

static volatile uint8_t *regs;
static int present;
static uint32_t db_stride;

static volatile uint32_t *asq, *acq, *iosq, *iocq;
static uint16_t asq_tail, acq_head, acq_phase;
static uint16_t iosq_tail, iocq_head, iocq_phase;
static uint16_t next_cid = 1;
static uint8_t *bounce;

static inline uint32_t rr32(uint32_t o) { return *(volatile uint32_t *)(regs + o); }
static inline uint64_t rr64(uint32_t o) { return *(volatile uint64_t *)(regs + o); }
static inline void wr32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(regs + o) = v; }
static inline void wr64(uint32_t o, uint64_t v) { *(volatile uint64_t *)(regs + o) = v; }

static void ring_sq(uint16_t qid, uint16_t tail) {
    __asm__ volatile ("mfence" ::: "memory");
    wr32(0x1000u + (uint32_t)(2 * qid) * db_stride, tail);
}
static void ring_cq(uint16_t qid, uint16_t head) {
    __asm__ volatile ("mfence" ::: "memory");
    wr32(0x1000u + (uint32_t)(2 * qid + 1) * db_stride, head);
}

static int wait_rdy(int want) {
    for (int i = 0; i < 20000000; i++) {
        uint32_t s = rr32(NVME_CSTS);
        if (want && (s & CSTS_RDY)) return 0;
        if (!want && !(s & CSTS_RDY)) return 0;
        __asm__ volatile ("pause");
    }
    return -1;
}

/* Fill one 64-byte SQE as 16 dwords */
static int poll_cq(volatile uint32_t *cq, uint16_t *head, uint16_t *phase,
                   uint16_t qid, uint32_t *status_out) {
    for (int spin = 0; spin < 20000000; spin++) {
        volatile uint32_t *e = cq + (size_t)(*head) * 4;
        uint32_t dw3 = e[3];
        uint32_t p = (dw3 >> 16) & 1u;
        if (p == *phase) {
            uint32_t st = (dw3 >> 17) & 0x7FFFu;
            if (status_out) *status_out = st;
            *head = (uint16_t)((*head + 1) % QDEPTH);
            if (*head == 0) *phase ^= 1;
            ring_cq(qid, *head);
            return st == 0 ? 0 : -1;
        }
        __asm__ volatile ("pause");
    }
    return -2;
}

static int submit(volatile uint32_t *sq, uint16_t *tail, uint16_t qid,
                  volatile uint32_t *cq, uint16_t *head, uint16_t *phase,
                  volatile uint32_t *sqe) {
    uint16_t t = *tail;
    volatile uint32_t *dst = sq + (size_t)t * 16;
    for (int i = 0; i < 16; i++) dst[i] = sqe[i];
    /* CID in CDW0 bits 31:16 */
    dst[0] = (dst[0] & 0xFFFFu) | ((uint32_t)next_cid++ << 16);
    if (next_cid == 0) next_cid = 1;
    *tail = (uint16_t)((t + 1) % QDEPTH);
    ring_sq(qid, *tail);
    uint32_t st = 0;
    int r = poll_cq(cq, head, phase, qid, &st);
    if (r == -2) {
        serial_write("nvme: timeout q=");
        serial_write_dec(qid);
        serial_write(" opc=");
        serial_write_hex(sqe[0] & 0xFF);
        serial_write("\n");
        return -1;
    }
    if (r != 0) {
        serial_write("nvme: status=");
        serial_write_hex(st);
        serial_write(" opc=");
        serial_write_hex(sqe[0] & 0xFF);
        serial_write("\n");
        return -1;
    }
    return 0;
}

static int find_bar(uint64_t *out) {
    for (int bus = 0; bus < 8; bus++)
    for (int slot = 0; slot < 32; slot++)
    for (int fn = 0; fn < 8; fn++) {
        if (pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0) == 0xFFFF) {
            if (fn == 0) break;
            continue;
        }
        if (pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0B) != 0x01) continue;
        if (pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0A) != 0x08) continue;

        uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)(slot & 31) << 11)
            | ((uint32_t)(fn & 7) << 8) | 0x04;
        __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
        uint32_t cmd;
        __asm__ volatile ("inl %1, %0" : "=a"(cmd) : "Nd"((uint16_t)0xCFC));
        cmd |= 0x06;
        __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
        __asm__ volatile ("outl %0, %1" :: "a"(cmd), "Nd"((uint16_t)0xCFC));

        uint32_t b0 = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x10);
        if (b0 & 4) {
            uint32_t hi = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x14);
            *out = ((uint64_t)hi << 32) | (b0 & ~0xFULL);
        } else
            *out = b0 & ~0xFULL;
        serial_write("nvme: PCI ");
        serial_write_dec((uint64_t)bus);
        serial_putc(':');
        serial_write_dec((uint64_t)slot);
        serial_write(" BAR0=");
        serial_write_hex(*out);
        serial_write("\n");
        return 0;
    }
    return -1;
}

int nvme_init(void) {
    present = 0;
    bounce = 0;
    uint64_t bar = 0;
    if (find_bar(&bar) != 0) {
        serial_write("nvme: no controller\n");
        return -1;
    }
    if (vmm_map_physical_range(bar, 0x20000) != 0) {
        serial_write("nvme: map failed\n");
        return -1;
    }
    regs = (volatile uint8_t *)(uintptr_t)bar;

    uint64_t cap = rr64(NVME_CAP);
    db_stride = 4u << ((cap >> 32) & 0xF);

    wr32(NVME_CC, 0);
    if (wait_rdy(0) != 0) {
        serial_write("nvme: disable timeout\n");
        return -1;
    }

    /* 4 pages: ASQ, ACQ, IOSQ, IOCQ — each holds QDEPTH entries */
    uint64_t pages = pmm_alloc_pages(4);
    if (!pages) return -1;
    uint8_t *base = (uint8_t *)(uintptr_t)pages;
    for (int i = 0; i < 16384; i++) base[i] = 0;

    asq  = (volatile uint32_t *)(base + 0);
    acq  = (volatile uint32_t *)(base + 4096);
    iosq = (volatile uint32_t *)(base + 8192);
    iocq = (volatile uint32_t *)(base + 12288);

    asq_tail = acq_head = 0; acq_phase = 1;
    iosq_tail = iocq_head = 0; iocq_phase = 1;
    next_cid = 1;

    wr32(NVME_AQA, ((QDEPTH - 1) << 16) | (QDEPTH - 1));
    wr64(NVME_ASQ, (uint64_t)(uintptr_t)asq);
    wr64(NVME_ACQ, (uint64_t)(uintptr_t)acq);

    /* EN | CSS=NVM | IOSQES=6 (64B) | IOCQES=4 (16B) */
    wr32(NVME_CC, CC_EN | (6u << 16) | (4u << 20));
    if (wait_rdy(1) != 0) {
        serial_write("nvme: enable timeout\n");
        return -1;
    }

    uint32_t tmp[16];

    /* CREATE_IO_CQ qid=1 */
    for (int i = 0; i < 16; i++) tmp[i] = 0;
    tmp[0] = 0x05;
    tmp[6] = (uint32_t)(uintptr_t)iocq;          /* prp1 lo */
    tmp[7] = (uint32_t)((uintptr_t)iocq >> 32);  /* prp1 hi */
    tmp[10] = ((QDEPTH - 1) << 16) | 1;          /* size | qid */
    tmp[11] = 1;                                 /* PC=1 */
    if (submit(asq, &asq_tail, 0, acq, &acq_head, &acq_phase, tmp) != 0) {
        serial_write("nvme: create CQ failed\n");
        return -1;
    }

    /* CREATE_IO_SQ qid=1, cqid=1 */
    for (int i = 0; i < 16; i++) tmp[i] = 0;
    tmp[0] = 0x01;
    tmp[6] = (uint32_t)(uintptr_t)iosq;
    tmp[7] = (uint32_t)((uintptr_t)iosq >> 32);
    tmp[10] = ((QDEPTH - 1) << 16) | 1;
    tmp[11] = (1u << 16) | 1; /* cqid | PC */
    if (submit(asq, &asq_tail, 0, acq, &acq_head, &acq_phase, tmp) != 0) {
        serial_write("nvme: create SQ failed\n");
        return -1;
    }

    {
        uint64_t bp = pmm_alloc_page();
        if (!bp) return -1;
        bounce = (uint8_t *)(uintptr_t)bp;
    }

    present = 1;
    serial_write("nvme: ready (NSID 1)\n");

    /* Immediate R/W self-test on LBA 200 (away from MBR) */
    for (int i = 0; i < 512; i++) bounce[i] = (uint8_t)(0x5A ^ i);
    if (nvme_write(200, 1, bounce) != 0)
        serial_write("nvme: self-write FAIL\n");
    else {
        for (int i = 0; i < 512; i++) bounce[i] = 0;
        if (nvme_read(200, 1, bounce) != 0)
            serial_write("nvme: self-read FAIL\n");
        else if (bounce[0] == (uint8_t)(0x5A ^ 0) && bounce[100] == (uint8_t)(0x5A ^ 100))
            serial_write("nvme: self-RW OK\n");
        else
            serial_write("nvme: self-RW data mismatch\n");
    }
    return 0;
}

int nvme_present(void) { return present; }

static int do_rw(uint64_t lba, uint32_t count, void *buf, int is_write) {
    if (!present || count == 0 || count > 8) return -1;
    size_t n = (size_t)count * 512;
    if (is_write) {
        for (size_t i = 0; i < n; i++) bounce[i] = ((const uint8_t *)buf)[i];
        __asm__ volatile ("mfence" ::: "memory");
    }

    uint32_t tmp[16];
    for (int i = 0; i < 16; i++) tmp[i] = 0;
    tmp[0] = is_write ? 0x01u : 0x02u; /* Write / Read */
    tmp[1] = 1; /* NSID */
    tmp[6] = (uint32_t)(uintptr_t)bounce;
    tmp[7] = (uint32_t)((uintptr_t)bounce >> 32);
    tmp[10] = (uint32_t)lba;
    tmp[11] = (uint32_t)(lba >> 32);
    tmp[12] = count - 1; /* 0-based NLB */

    if (submit(iosq, &iosq_tail, 1, iocq, &iocq_head, &iocq_phase, tmp) != 0)
        return -1;

    if (!is_write) {
        for (size_t i = 0; i < n; i++) ((uint8_t *)buf)[i] = bounce[i];
    }
    return 0;
}

int nvme_read(uint64_t lba, uint32_t count, void *buf) {
    return do_rw(lba, count, buf, 0);
}
int nvme_write(uint64_t lba, uint32_t count, const void *buf) {
    return do_rw(lba, count, (void *)buf, 1);
}
