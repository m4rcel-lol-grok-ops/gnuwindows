#ifndef GW_ATA_H
#define GW_ATA_H
#include <stdint.h>
#include <stddef.h>
int  ata_init(void);
int  ata_read_sectors(uint32_t lba, uint32_t count, void *buf);
int  ata_write_sectors(uint32_t lba, uint32_t count, const void *buf);
void ata_flush(void);
#endif
