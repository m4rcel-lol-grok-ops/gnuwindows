#ifndef GW_HEAP_H
#define GW_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the kernel heap. Must be called after pmm_init(). */
int  heap_init(void);

/* Allocate `size` bytes. Alignment is at least 16 bytes.
 * Returns NULL on out-of-memory. */
void *kmalloc(size_t size);

/* Allocate `size` bytes aligned to `align` (power of two, >= 16). */
void *kmalloc_aligned(size_t size, size_t align);

/* Free a block previously returned by kmalloc / kmalloc_aligned.
 * NULL is a no-op. Double-free is detected and reported. */
void  kfree(void *ptr);

/* Statistics (bytes) */
size_t heap_used_bytes(void);
size_t heap_free_bytes(void);
size_t heap_total_bytes(void);

#endif
