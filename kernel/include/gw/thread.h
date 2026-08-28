#ifndef GW_THREAD_H
#define GW_THREAD_H

#include <stdint.h>
#include <stddef.h>

#define MAX_THREADS       16
#define THREAD_STACK_SIZE (8192)
#define THREAD_NAME_MAX   24

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
    int            id;
    thread_state_t state;
    char           name[THREAD_NAME_MAX];
    void         (*entry)(void *);
    void          *arg;
    uint64_t       rsp;
    uint64_t       stack_top;
    uint8_t       *stack_base;
    struct thread *next;
} thread_t;

thread_t *thread_current(void);
void      thread_set_current(thread_t *t);
int       thread_create(const char *name, void (*entry)(void *), void *arg);
void      thread_yield(void);
void      thread_maybe_yield(void); /* yield if timer asked for resched */
void      thread_exit(void);
void      thread_init_idle(thread_t *idle);

#endif
