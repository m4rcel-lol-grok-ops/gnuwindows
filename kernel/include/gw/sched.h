#ifndef GW_SCHED_H
#define GW_SCHED_H

#include <gw/thread.h>

void sched_init(void);
void sched_start(void);           /* never returns — enters idle + enables preemption */
void sched_add(thread_t *t);
void sched_on_timer(void);        /* called from timer IRQ */
thread_t *sched_pick_next(void);

#endif
