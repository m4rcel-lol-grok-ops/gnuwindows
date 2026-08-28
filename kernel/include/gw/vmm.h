#ifndef GW_VMM_H
#define GW_VMM_H

#include <stdint.h>

/* Build identity map with User bit so ring3 can run */
int  vmm_init(void);
void vmm_load(void);
int  vmm_map_physical_range(uint64_t phys, uint64_t size);

#endif
