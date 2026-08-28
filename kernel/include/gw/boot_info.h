#ifndef GW_BOOT_INFO_H
#define GW_BOOT_INFO_H

#include <stdint.h>

#define GW_BOOT_MAGIC 0x31425747ULL  /* 'GWB1' */

typedef struct {
    uint64_t magic;
    uint64_t kernel_physical_base;
    uint64_t kernel_entry;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;
    uint64_t memory_map_desc_version;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint64_t rsdp;
} GW_BOOT_INFO;

#endif
