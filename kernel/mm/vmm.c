/*
 * Identity map first 4 GiB with 2 MiB pages, User|Write|Present
 */

#include <gw/vmm.h>
#include <gw/pmm.h>
#include <gw/serial.h>
#include <stdint.h>

#define PTE_P  1ULL
#define PTE_W  2ULL
#define PTE_U  4ULL
#define PTE_PS (1ULL << 7)
#define PTE_FLAGS (PTE_P | PTE_W | PTE_U)

static uint64_t *pml4;

static uint64_t *alloc_table(void) {
    uint64_t p = pmm_alloc_page();
    if (!p) return 0;
    uint64_t *t = (uint64_t *)(uintptr_t)p;
    for (int i = 0; i < 512; i++) t[i] = 0;
    return t;
}

int vmm_init(void) {
    pml4 = alloc_table();
    if (!pml4) return -1;

    uint64_t *pdpt = alloc_table();
    if (!pdpt) return -1;
    pml4[0] = (uint64_t)(uintptr_t)pdpt | PTE_FLAGS;

    /* 4 x 1 GiB regions via 2 MiB pages */
    for (int g = 0; g < 4; g++) {
        uint64_t *pd = alloc_table();
        if (!pd) return -1;
        pdpt[g] = (uint64_t)(uintptr_t)pd | PTE_FLAGS;
        for (int i = 0; i < 512; i++) {
            uint64_t phys = (uint64_t)g * 0x40000000ULL + (uint64_t)i * 0x200000ULL;
            pd[i] = phys | PTE_FLAGS | PTE_PS;
        }
    }

    serial_write("VMM: identity 0-4GiB (2MiB pages, user R/W)\n");
    return 0;
}

void vmm_load(void) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4) : "memory");
}
