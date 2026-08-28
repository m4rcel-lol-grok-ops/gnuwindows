#ifndef GW_NVME_H
#define GW_NVME_H

#include <stdint.h>
#include <stddef.h>

int  nvme_init(void);
int  nvme_present(void);
int  nvme_read(uint64_t lba, uint32_t count, void *buf);
int  nvme_write(uint64_t lba, uint32_t count, const void *buf);

#endif
