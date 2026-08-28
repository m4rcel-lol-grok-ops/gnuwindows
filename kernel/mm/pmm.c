/*
 * GWKernel Physical Memory Manager - Phase 3
 *
 * Bitmap-based allocator over 4 KiB pages.
 * Parses the UEFI memory map provided by the bootloader.
 * Only EfiConventionalMemory / BootServices* / Loader* regions
 * that survive ExitBootServices are treated as free initially;
 * the kernel image, boot_info, and early stack are marked used.
 */

#include <gw/pmm.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

/* UEFI memory types (subset) */
enum {
    EfiReservedMemoryType      = 0,
    EfiLoaderCode              = 1,
    EfiLoaderData              = 2,
    EfiBootServicesCode        = 3,
    EfiBootServicesData        = 4,
    EfiRuntimeServicesCode     = 5,
    EfiRuntimeServicesData     = 6,
    EfiConventionalMemory      = 7,
    EfiUnusableMemory          = 8,
    EfiACPIReclaimMemory       = 9,
    EfiACPIMemoryNVS           = 10,
    EfiMemoryMappedIO          = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode                 = 13,
    EfiPersistentMemory        = 14
};

typedef struct {
    uint32_t Type;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} efi_memory_descriptor_t;

/* Bitmap: 1 = used, 0 = free
 * Supports up to 4 GiB of physical memory (1M pages → 128 KiB bitmap).
 * QEMU is started with 256 MiB, so this is more than enough for Phase 3.
 */
#define PMM_MAX_PAGES   (1024ULL * 1024)          /* 4 GiB / 4 KiB */
#define PMM_BITMAP_BYTES (PMM_MAX_PAGES / 8)

static uint8_t  pmm_bitmap[PMM_BITMAP_BYTES];
static uint64_t pmm_total;
static uint64_t pmm_used;
static uint64_t pmm_highest_page;   /* highest page index we manage */
static int      pmm_ready;

static inline void bitmap_set(uint64_t page) {
    if (page >= PMM_MAX_PAGES) return;
    pmm_bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}

static inline void bitmap_clear(uint64_t page) {
    if (page >= PMM_MAX_PAGES) return;
    pmm_bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static inline int bitmap_test(uint64_t page) {
    if (page >= PMM_MAX_PAGES) return 1; /* treat out-of-range as used */
    return (pmm_bitmap[page / 8] >> (page % 8)) & 1;
}

static void mark_range_used(uint64_t phys, uint64_t size) {
    if (size == 0) return;
    uint64_t start = phys / PAGE_SIZE;
    uint64_t end   = (phys + size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (end > PMM_MAX_PAGES) end = PMM_MAX_PAGES;
    for (uint64_t p = start; p < end; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            pmm_used++;
        }
    }
}

void pmm_mark_used(uint64_t phys, uint64_t size) {
    mark_range_used(phys, size);
}

int pmm_init(const GW_BOOT_INFO *bi) {
    if (!bi || bi->magic != GW_BOOT_MAGIC || !bi->memory_map) {
        serial_write("PMM: invalid boot_info\n");
        return -1;
    }

    /* Start with everything marked used; then free usable regions. */
    for (uint64_t i = 0; i < PMM_BITMAP_BYTES; i++)
        pmm_bitmap[i] = 0xFF;

    pmm_total = 0;
    pmm_used  = 0;
    pmm_highest_page = 0;

    uint8_t *map = (uint8_t *)bi->memory_map;
    uint8_t *end = map + bi->memory_map_size;
    uint64_t desc_size = bi->memory_map_desc_size;
    if (desc_size < sizeof(efi_memory_descriptor_t))
        desc_size = sizeof(efi_memory_descriptor_t);

    while (map + desc_size <= end) {
        efi_memory_descriptor_t *d = (efi_memory_descriptor_t *)map;
        uint64_t start_page = d->PhysicalStart / PAGE_SIZE;
        uint64_t npages     = d->NumberOfPages;

        if (start_page + npages > pmm_highest_page)
            pmm_highest_page = start_page + npages;

        /* Usable after ExitBootServices */
        int usable = (d->Type == EfiConventionalMemory ||
                      d->Type == EfiBootServicesCode  ||
                      d->Type == EfiBootServicesData  ||
                      d->Type == EfiLoaderCode        ||
                      d->Type == EfiLoaderData);

        if (usable && start_page < PMM_MAX_PAGES) {
            uint64_t last = start_page + npages;
            if (last > PMM_MAX_PAGES) last = PMM_MAX_PAGES;
            for (uint64_t p = start_page; p < last; p++) {
                if (bitmap_test(p)) {
                    bitmap_clear(p);
                    /* will recount used below */
                }
            }
            pmm_total += (last - start_page);
        }

        map += desc_size;
    }

    if (pmm_highest_page > PMM_MAX_PAGES)
        pmm_highest_page = PMM_MAX_PAGES;

    /* Recount used pages inside the managed range */
    pmm_used = 0;
    for (uint64_t p = 0; p < pmm_highest_page; p++) {
        if (bitmap_test(p))
            pmm_used++;
    }

    /* Protect critical regions that must never be given out */
    /* 1. First 1 MiB (legacy / BIOS / bootloader remnants) */
    mark_range_used(0, 0x100000);

    /* 2. Kernel image (linked & loaded at 0x100000, size from boot_info) */
    if (bi->kernel_physical_base) {
        /* Approximate kernel size: from base up to a safe upper bound.
           Phase 1/2 kernel is < 64 KiB; mark 256 KiB to be safe. */
        mark_range_used(bi->kernel_physical_base, 1024 * 1024);  /* 1 MiB covers bitmap + code */
    }

    /* 3. Early stack we are using (0x80000–0x90000) */
    mark_range_used(0x80000, 0x10000);

    /* 4. The bitmap itself lives in kernel .bss → already covered by kernel mark.
       5. boot_info page */
    if (bi) {
        mark_range_used((uint64_t)bi, PAGE_SIZE);
    }
    /* 6. Memory map itself */
    if (bi->memory_map && bi->memory_map_size)
        mark_range_used(bi->memory_map, bi->memory_map_size);

    pmm_ready = 1;

    serial_write("PMM: initialized\n");
    serial_write("  Managed pages: ");
    serial_write_dec(pmm_highest_page);
    serial_write("\n  Free pages:    ");
    serial_write_dec(pmm_get_free_pages());
    serial_write("\n  Used pages:    ");
    serial_write_dec(pmm_get_used_pages());
    serial_write("\n");

    return 0;
}

uint64_t pmm_alloc_page(void) {
    if (!pmm_ready) return 0;

    for (uint64_t p = 0; p < pmm_highest_page; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            pmm_used++;
            return p * PAGE_SIZE;
        }
    }
    return 0; /* out of memory */
}

uint64_t pmm_alloc_pages(size_t count) {
    if (!pmm_ready || count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    uint64_t run = 0;
    uint64_t start = 0;

    for (uint64_t p = 0; p < pmm_highest_page; p++) {
        if (!bitmap_test(p)) {
            if (run == 0) start = p;
            run++;
            if (run == count) {
                for (uint64_t i = 0; i < count; i++) {
                    bitmap_set(start + i);
                    pmm_used++;
                }
                return start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

void pmm_free_page(uint64_t phys) {
    if (!pmm_ready || (phys & (PAGE_SIZE - 1))) return;
    uint64_t p = phys / PAGE_SIZE;
    if (p >= pmm_highest_page) return;
    if (!bitmap_test(p)) {
        /* double-free detection */
        serial_write("PMM WARNING: double-free of page ");
        serial_write_hex(phys);
        serial_write("\n");
        return;
    }
    bitmap_clear(p);
    if (pmm_used) pmm_used--;
}

void pmm_free_pages(uint64_t phys, size_t count) {
    for (size_t i = 0; i < count; i++)
        pmm_free_page(phys + i * PAGE_SIZE);
}

uint64_t pmm_get_total_pages(void) {
    return pmm_highest_page;
}

uint64_t pmm_get_used_pages(void) {
    return pmm_used;
}

uint64_t pmm_get_free_pages(void) {
    if (pmm_highest_page < pmm_used) return 0;
    return pmm_highest_page - pmm_used;
}
