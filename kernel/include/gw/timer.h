#ifndef GW_TIMER_H
#define GW_TIMER_H

#include <stdint.h>

void timer_init(void);
uint64_t timer_ticks(void);
void timer_handler(void);
void timer_preempt_check(void);
int  timer_need_resched(void);

#endif
