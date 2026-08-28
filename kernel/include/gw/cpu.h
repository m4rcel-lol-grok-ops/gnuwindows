#ifndef GW_CPU_H
#define GW_CPU_H

#include <stdint.h>

void cpu_init(void);
void cpu_enable_interrupts(void);
void cpu_disable_interrupts(void);
void cpu_test_exceptions(void);

#endif
