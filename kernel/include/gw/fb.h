#ifndef GW_FB_H
#define GW_FB_H

#include <stdint.h>
#include <gw/boot_info.h>

void fb_init(GW_BOOT_INFO *bi);
void fb_putc(char c);
void fb_write(const char *s);
int  fb_active(void);

/* Map framebuffer into identity page tables if outside 0-4GiB */
int  vmm_map_physical_range(uint64_t phys, uint64_t size);

#endif
