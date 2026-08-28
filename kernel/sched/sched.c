/*
 * Round-robin scheduler — cooperative yield among threads with stacks.
 * Never context-switch to idle (idle has no stack; kmain HLT loop is fine).
 */

#include <gw/sched.h>
#include <gw/thread.h>
#include <gw/timer.h>
#include <gw/serial.h>
#include <stdint.h>

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void thread_set_current(thread_t *t);
extern void thread_init_idle(thread_t *idle);

static thread_t idle_thread;
static thread_t *run_queue;
static int sched_started;

void sched_add(thread_t *t) {
    t->next = NULL;
    if (!run_queue) {
        run_queue = t;
        return;
    }
    thread_t *p = run_queue;
    while (p->next) p = p->next;
    p->next = t;
}

static thread_t *dequeue(void) {
    thread_t *t = run_queue;
    if (t) {
        run_queue = t->next;
        t->next = NULL;
    }
    return t;
}

thread_t *sched_pick_next(void) {
    thread_t *t;
    int scanned = 0;
    while (scanned < MAX_THREADS) {
        t = dequeue();
        if (!t) break;
        scanned++;
        if (t->state == THREAD_READY)
            return t;
        if (t->state == THREAD_DEAD)
            continue;
        /* RUNNING should not be in queue */
    }
    return NULL; /* no ready thread */
}

void sched_init(void) {
    run_queue = NULL;
    sched_started = 0;
    thread_init_idle(&idle_thread);
    serial_write("sched: RR cooperative\n");
}

void sched_on_timer(void) {
    if (!sched_started)
        return;

    thread_t *cur = thread_current();
    thread_t *next = sched_pick_next();

    /* Nothing else runnable: keep current (do NOT switch to idle rsp=0) */
    if (!next) {
        if (cur && cur->state == THREAD_RUNNING)
            return;
        return;
    }

    if (cur == next)
        return;

    if (cur && cur != &idle_thread && cur->state == THREAD_RUNNING) {
        cur->state = THREAD_READY;
        sched_add(cur);
    }

    next->state = THREAD_RUNNING;
    thread_set_current(next);

    if (cur && cur != &idle_thread)
        context_switch(&cur->rsp, next->rsp);
    else {
        uint64_t dummy = 0;
        context_switch(&dummy, next->rsp);
    }
}

void sched_start(void) {
    sched_started = 1;
    serial_write("sched: starting\n");

    thread_t *next = sched_pick_next();
    if (!next) {
        serial_write("sched: no threads\n");
        for (;;) __asm__ volatile ("hlt");
    }
    next->state = THREAD_RUNNING;
    thread_set_current(next);

    uint64_t discard = 0;
    context_switch(&discard, next->rsp);

    /* If we ever return here (shouldn't), idle HLT */
    for (;;) {
        if (timer_need_resched())
            sched_on_timer();
        __asm__ volatile ("hlt");
    }
}
