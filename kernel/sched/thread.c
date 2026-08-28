/*
 * Kernel threads - Phase 6
 */

#include <gw/thread.h>
#include <gw/sched.h>
#include <gw/heap.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

extern void thread_trampoline(void);
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

static thread_t thread_pool[MAX_THREADS];
static thread_t *current;
static int next_id = 1;


static void name_copy(char *dst, const char *src) {
    size_t i = 0;
    if (!src) src = "?";
    while (src[i] && i + 1 < THREAD_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

thread_t *thread_current(void) {
    return current;
}

void thread_set_current(thread_t *t) {
    current = t;
}

int thread_create(const char *name, void (*entry)(void *), void *arg) {
    thread_t *t = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (thread_pool[i].state == THREAD_UNUSED) {
            t = &thread_pool[i];
            break;
        }
    }
    if (!t) {
        serial_write("thread_create: no free slot\n");
        return -1;
    }

    uint8_t *stack = kmalloc(THREAD_STACK_SIZE);
    if (!stack) {
        serial_write("thread_create: stack alloc failed\n");
        return -1;
    }

    t->id = next_id++;
    t->state = THREAD_READY;
    t->entry = entry;
    t->arg = arg;
    t->stack_base = stack;
    t->next = NULL;
    name_copy(t->name, name);

    /* Build initial stack frame (grows down).
       Layout at switch-in:
         [r15][r14][r13][r12][rbx][rbp][ret=trampoline][arg][entry]
       context_switch pops r15..rbp then ret -> trampoline
       trampoline pops arg, entry, calls entry
    */
    uint64_t *sp = (uint64_t *)(stack + THREAD_STACK_SIZE);
    /* align */
    sp = (uint64_t *)((uint64_t)sp & ~0xFUL);

    *(--sp) = (uint64_t)entry;              /* for trampoline */
    *(--sp) = (uint64_t)arg;
    *(--sp) = (uint64_t)thread_trampoline;  /* return address for context_switch */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;
    t->stack_top = (uint64_t)(stack + THREAD_STACK_SIZE);
    if (t->rsp < (uint64_t)stack || t->rsp >= t->stack_top) {
        serial_write("thread: bad stack frame\n");
        kfree(stack);
        t->state = THREAD_UNUSED;
        return -1;
    }

    sched_add(t);

    serial_write("thread: created \"");
    serial_write(t->name);
    serial_write("\" id=");
    serial_write_dec((uint64_t)t->id);
    serial_write("\n");
    return t->id;
}

void thread_yield(void) {
    sched_on_timer(); /* reuse: pick next and switch */
}

void thread_maybe_yield(void) {
    extern int timer_need_resched(void);
    /* Only switch when scheduler is live and we have a real thread stack */
    if (!thread_current() || !thread_current()->stack_base)
        return;
    if (timer_need_resched())
        sched_on_timer();
}

void thread_exit(void) {
    thread_t *t = current;
    if (t) {
        t->state = THREAD_DEAD;
        serial_write("thread: \"");
        serial_write(t->name);
        serial_write("\" exited\n");
    }
    /* Never return — switch away */
    for (;;) {
        sched_on_timer();
        __asm__ volatile ("hlt");
    }
}

void thread_init_idle(thread_t *idle) {
    for (int i = 0; i < MAX_THREADS; i++)
        thread_pool[i].state = THREAD_UNUSED;

    idle->id = 0;
    idle->state = THREAD_RUNNING;
    idle->stack_base = NULL;
    idle->rsp = 0;
    idle->next = NULL;
    name_copy(idle->name, "idle");
    current = idle;
}
