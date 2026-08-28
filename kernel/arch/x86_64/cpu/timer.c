/*
 * System timer (IRQ0) — tick counter + reschedule flag.
 * Context switch is NEVER done from IRQ stack (that corrupted RSP and
 * caused page faults at heap free-magic addresses). Threads preempt
 * cooperatively via thread_yield() / idle loop checking the flag.
 */

#include <gw/timer.h>
#include <gw/pic.h>
#include <gw/pit.h>
#include <gw/serial.h>
#include <stdint.h>

static volatile uint64_t ticks;
static volatile int need_resched;

void timer_handler(void) {
    ticks++;
    need_resched = 1;
}

void timer_preempt_check(void) {
    /* Intentionally empty: do not context_switch from IRQ.
     * EOI path returns to interrupted thread; yield/idle will switch. */
}

int timer_need_resched(void) {
    int v = need_resched;
    need_resched = 0;
    return v;
}

uint64_t timer_ticks(void) {
    return ticks;
}

void timer_init(void) {
    ticks = 0;
    need_resched = 0;
    pit_init(100);
    pic_unmask(0);
    serial_write("Timer: PIT @ ~100 Hz (coop yield, no IRQ switch)\n");
}
