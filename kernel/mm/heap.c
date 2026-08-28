/*
 * GWKernel Kernel Heap - Phase 4
 *
 * Simple free-list allocator on top of the PMM.
 * - 16-byte minimum alignment
 * - Header before every block
 * - Coalescing of adjacent free blocks
 * - Debug: poison patterns, double-free detection, canary
 */

#include <gw/heap.h>
#include <gw/pmm.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define HEAP_ALIGN          16
#define HEAP_MAGIC_ALLOC    0xA110C000u
#define HEAP_MAGIC_FREE     0xF4EE0000u
#define HEAP_POISON_ALLOC   0xCDu
#define HEAP_POISON_FREE    0xDCu
#define HEAP_MIN_BLOCK      32          /* header + minimal payload */
#define HEAP_EXPAND_PAGES   16          /* grow by 64 KiB at a time */

typedef struct heap_block {
    uint32_t magic;             /* ALLOC or FREE */
    uint32_t size;              /* payload size in bytes (not including header) */
    struct heap_block *next;    /* free-list link (only valid when FREE) */
    uint32_t canary;            /* ~magic for basic corruption check */
} heap_block_t;

/* Free list is sorted by address for simple coalescing */
static heap_block_t *free_list;
static size_t heap_total;
static size_t heap_used;
static int    heap_ready;

static void *memset_local(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static inline size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

static inline int is_power_of_two(size_t v) {
    return v && !(v & (v - 1));
}

static void insert_free(heap_block_t *block) {
    block->magic  = HEAP_MAGIC_FREE;
    block->canary = ~HEAP_MAGIC_FREE;
    block->next   = NULL;

    /* Poison payload */
    memset_local((uint8_t *)block + sizeof(heap_block_t),
                 HEAP_POISON_FREE, block->size);

    /* Insert sorted by address + coalesce */
    heap_block_t **pp = &free_list;
    while (*pp && (uint8_t *)*pp < (uint8_t *)block)
        pp = &(*pp)->next;

    /* Coalesce with next */
    if (*pp) {
        uint8_t *block_end = (uint8_t *)block + sizeof(heap_block_t) + block->size;
        if (block_end == (uint8_t *)*pp) {
            block->size += sizeof(heap_block_t) + (*pp)->size;
            block->next  = (*pp)->next;
            *pp = block;
            return;
        }
    }

    block->next = *pp;
    *pp = block;

    /* Coalesce with previous (re-walk to find prev) */
    if (free_list != block) {
        heap_block_t *prev = free_list;
        while (prev && prev->next != block)
            prev = prev->next;
        if (prev) {
            uint8_t *prev_end = (uint8_t *)prev + sizeof(heap_block_t) + prev->size;
            if (prev_end == (uint8_t *)block) {
                prev->size += sizeof(heap_block_t) + block->size;
                prev->next  = block->next;
            }
        }
    }
}

static int expand_heap(size_t need_payload) {
    /* Request enough pages for header + payload + a bit of slack */
    size_t need = sizeof(heap_block_t) + need_payload + HEAP_MIN_BLOCK;
    size_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < HEAP_EXPAND_PAGES)
        pages = HEAP_EXPAND_PAGES;

    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys)
        return -1;

    heap_block_t *block = (heap_block_t *)(uintptr_t)phys;
    block->size = (uint32_t)(pages * PAGE_SIZE - sizeof(heap_block_t));
    heap_total += pages * PAGE_SIZE;

    insert_free(block);
    return 0;
}

int heap_init(void) {
    free_list  = NULL;
    heap_total = 0;
    heap_used  = 0;

    if (expand_heap(PAGE_SIZE) != 0) {
        serial_write("HEAP: initial expand failed\n");
        return -1;
    }

    heap_ready = 1;
    serial_write("HEAP: initialized (");
    serial_write_dec(heap_total);
    serial_write(" bytes)\n");
    return 0;
}

void *kmalloc(size_t size) {
    return kmalloc_aligned(size, HEAP_ALIGN);
}

void *kmalloc_aligned(size_t size, size_t align) {
    if (!heap_ready || size == 0)
        return NULL;
    if (!is_power_of_two(align) || align < HEAP_ALIGN)
        align = HEAP_ALIGN;

    /* For alignment > natural header alignment we over-allocate and
       store the real block pointer just before the returned address. */
    size_t need = size;
    int over_aligned = (align > HEAP_ALIGN);

    if (over_aligned) {
        /* Extra space for alignment padding + a pointer to the real block */
        need = size + align + sizeof(void *);
    } else {
        need = align_up(size, HEAP_ALIGN);
    }
    if (need < 16)
        need = 16;

    for (;;) {
        heap_block_t **pp = &free_list;
        while (*pp) {
            heap_block_t *b = *pp;

            if (b->magic != HEAP_MAGIC_FREE || b->canary != (uint32_t)~HEAP_MAGIC_FREE) {
                serial_write("HEAP: free-list corruption detected\n");
                return NULL;
            }

            if (b->size >= need) {
                *pp = b->next;

                size_t remain = b->size - need;
                if (remain >= sizeof(heap_block_t) + HEAP_MIN_BLOCK) {
                    heap_block_t *rest = (heap_block_t *)((uint8_t *)b +
                                           sizeof(heap_block_t) + need);
                    rest->size = (uint32_t)(remain - sizeof(heap_block_t));
                    insert_free(rest);
                    b->size = (uint32_t)need;
                }

                b->magic  = HEAP_MAGIC_ALLOC;
                b->canary = ~HEAP_MAGIC_ALLOC;
                b->next   = NULL;

                uint8_t *base = (uint8_t *)b + sizeof(heap_block_t);
                void *ptr;

                if (over_aligned) {
                    /* Align user pointer; store real block ptr immediately before it */
                    uintptr_t raw = (uintptr_t)(base + sizeof(void *));
                    uintptr_t aligned = (raw + align - 1) & ~(uintptr_t)(align - 1);
                    ptr = (void *)aligned;
                    *((heap_block_t **)ptr - 1) = b;
                } else {
                    ptr = base;
                }

                memset_local(ptr, HEAP_POISON_ALLOC, size);
                heap_used += b->size + sizeof(heap_block_t);
                return ptr;
            }
            pp = &b->next;
        }

        if (expand_heap(need) != 0)
            return NULL;
    }
}

void kfree(void *ptr) {
    if (!ptr || !heap_ready)
        return;

    /* First try the normal (16-byte aligned) case */
    heap_block_t *b = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    if (b->magic != HEAP_MAGIC_ALLOC || b->canary != (uint32_t)~HEAP_MAGIC_ALLOC) {
        /* Maybe an over-aligned pointer: real block stored just before user ptr */
        heap_block_t *candidate = *((heap_block_t **)ptr - 1);
        if (candidate &&
            candidate->magic == HEAP_MAGIC_ALLOC &&
            candidate->canary == (uint32_t)~HEAP_MAGIC_ALLOC) {
            b = candidate;
        } else if (b->magic == HEAP_MAGIC_FREE) {
            serial_write("HEAP WARNING: double-free of ");
            serial_write_hex((uint64_t)(uintptr_t)ptr);
            serial_write("\n");
            return;
        } else {
            serial_write("HEAP: invalid or corrupted block at ");
            serial_write_hex((uint64_t)(uintptr_t)ptr);
            serial_write("\n");
            return;
        }
    }

    if (b->magic == HEAP_MAGIC_FREE) {
        serial_write("HEAP WARNING: double-free of ");
        serial_write_hex((uint64_t)(uintptr_t)ptr);
        serial_write("\n");
        return;
    }

    size_t total = b->size + sizeof(heap_block_t);
    if (heap_used >= total)
        heap_used -= total;
    else
        heap_used = 0;

    insert_free(b);
}

size_t heap_used_bytes(void)  { return heap_used; }
size_t heap_free_bytes(void)  {
    size_t free = 0;
    for (heap_block_t *b = free_list; b; b = b->next)
        free += b->size + sizeof(heap_block_t);
    return free;
}
size_t heap_total_bytes(void) { return heap_total; }
