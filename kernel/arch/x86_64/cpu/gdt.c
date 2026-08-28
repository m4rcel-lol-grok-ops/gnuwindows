/*
 * GDT with kernel + user segments and 64-bit TSS
 */

#include <gw/gdt.h>
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* 64-bit TSS system descriptor (16 bytes) */
struct gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

/* 5 normal + TSS takes 2 slots = indices 0..6 */
static struct gdt_entry gdt[8];
static struct gdt_ptr gdtr;
static struct tss tss;
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
    gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[idx].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[idx].access      = access;
}

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    struct gdt_tss_entry *t = (struct gdt_tss_entry *)&gdt[idx];
    t->limit_low   = (uint16_t)(limit & 0xFFFF);
    t->base_low    = (uint16_t)(base & 0xFFFF);
    t->base_mid    = (uint8_t)((base >> 16) & 0xFF);
    t->access      = 0x89; /* present, type = 64-bit TSS available */
    t->granularity = (uint8_t)((limit >> 16) & 0x0F);
    t->base_high   = (uint8_t)((base >> 24) & 0xFF);
    t->base_upper  = (uint32_t)(base >> 32);
    t->reserved    = 0;
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    for (int i = 0; i < 8; i++) {
        gdt[i].limit_low = gdt[i].base_low = 0;
        gdt[i].base_mid = gdt[i].access = 0;
        gdt[i].granularity = gdt[i].base_high = 0;
    }

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0); /* kernel code */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0); /* kernel data */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0); /* user code  ring3 */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0); /* user data  ring3 */

    for (unsigned i = 0; i < sizeof(tss); i++)
        ((uint8_t *)&tss)[i] = 0;
    tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    tss.iomap_base = sizeof(tss);
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss) - 1);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m"(gdtr)
        : "rax", "ax", "memory"
    );
}
