#ifndef GW_AHCI_H
#define GW_AHCI_H

#include <stdint.h>
#include <stddef.h>

int  ahci_init(void);
/* Read count 512-byte sectors starting at lba into buf. Returns 0 on success. */
int  ahci_read(uint64_t lba, uint32_t count, void *buf);
int  ahci_write(uint64_t lba, uint32_t count, const void *buf);
int  ahci_present(void);

#endif
