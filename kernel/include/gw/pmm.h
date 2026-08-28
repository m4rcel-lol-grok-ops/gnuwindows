#ifndef GW_PMM_H
#define GW_PMM_H

#include <stdint.h>
#include <stddef.h>
#include <gw/boot_info.h>

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12

int  pmm_init(const GW_BOOT_INFO *boot_info);

uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(size_t count);

void pmm_free_page(uint64_t phys);
void pmm_free_pages(uint64_t phys, size_t count);

/* Statistics */
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_used_pages(void);
uint64_t pmm_get_free_pages(void);

void pmm_mark_used(uint64_t phys, uint64_t size);

#endif
