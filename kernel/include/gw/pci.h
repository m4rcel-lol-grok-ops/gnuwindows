#ifndef GW_PCI_H
#define GW_PCI_H

#include <stdint.h>

void pci_init(void);
void pci_list(void); /* serial dump of functions found */

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

#endif
