#ifndef GW_ELF_H
#define GW_ELF_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t entry;
    uint64_t load_base;
    uint64_t load_size;
} elf_info_t;

/* Load ELF64 from memory buffer into identity-mapped space. */
int elf_load(const void *data, size_t size, elf_info_t *out);

/* Load path from VFS and enter ring 3. Returns exit code. */
int process_exec(const char *path);

#endif
