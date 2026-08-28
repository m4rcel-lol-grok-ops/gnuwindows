#ifndef GW_GDT_H
#define GW_GDT_H

#include <stdint.h>

#define GDT_NULL_SEL     0x00
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18   /* ring 3, RPL will be | 3 → 0x1B */
#define GDT_USER_DATA    0x20   /* ring 3, RPL | 3 → 0x23 */
#define GDT_TSS_SEL      0x28

void gdt_init(void);
void tss_set_kernel_stack(uint64_t rsp0);

#endif
