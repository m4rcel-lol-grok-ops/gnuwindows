/*
 * GWKernel syscall dispatcher - custom ABI
 */

#include <gw/syscall.h>
#include <gw/serial.h>
#include <gw/thread.h>
#include <gw/timer.h>
#include <gw/heap.h>
#include <gw/vfs.h>
#include <gw/gdt.h>
#include <gw/process.h>
#include <stdint.h>
#include <stddef.h>

/* Frame matches isr_common pushes + vector/error + iretq frame */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    (void)fd; /* stdout/stderr both serial for now */
    const char *s = (const char *)(uintptr_t)buf;
    for (uint64_t i = 0; i < len; i++) {
        if (!s[i] && i + 1 == len) break;
        serial_putc(s[i]);
    }
    return (int64_t)len;
}

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    (void)fd;
    char *d = (char *)(uintptr_t)buf;
    size_t n = 0;
    while (n < len) {
        int c = serial_getc_nonblock();
        if (c < 0) {
            if (n) break;
            thread_yield();
            continue;
        }
        d[n++] = (char)c;
        if (c == '\n' || c == '\r') break;
    }
    return (int64_t)n;
}

static int64_t sys_open(uint64_t path, uint64_t flags, uint64_t mode) {
    (void)flags; (void)mode;
    const char *p = (const char *)(uintptr_t)path;
    vfs_node_t *n = vfs_lookup(p);
    if (!n) {
        if (vfs_create_file(p) != 0) return -1;
        n = vfs_lookup(p);
    }
    if (!n) return -1;
    /* Return a fake fd = pointer tag index; simple: return 3+ as success token */
    return 3; /* demo: always fd 3 for last open — limited but works for sequential use */
}

static int64_t sys_exit(uint64_t code) {
    (void)code;
    /* Ring-3 exit is handled in syscall_entry_c via process_handle_exit */
    thread_exit();
    return 0;
}

static int64_t sys_getpid(void) {
    thread_t *t = thread_current();
    return t ? (int64_t)t->id : 0;
}

static int64_t sys_yield(void) {
    thread_yield();
    return 0;
}

static int64_t sys_sleep(uint64_t ticks_wait) {
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < ticks_wait)
        thread_yield();
    return 0;
}

static int64_t sys_alloc(uint64_t size) {
    void *p = kmalloc((size_t)size);
    return p ? (int64_t)(uintptr_t)p : -1;
}

static int64_t sys_free(uint64_t ptr) {
    kfree((void *)(uintptr_t)ptr);
    return 0;
}

int64_t syscall_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a3;
    switch (nr) {
    case SYS_EXIT:   return sys_exit(a0);
    case SYS_WRITE:  return sys_write(a0, a1, a2);
    case SYS_READ:   return sys_read(a0, a1, a2);
    case SYS_OPEN:   return sys_open(a0, a1, a2);
    case SYS_CLOSE:  return 0;
    case SYS_GETPID: return sys_getpid();
    case SYS_YIELD:  return sys_yield();
    case SYS_SLEEP:  return sys_sleep(a0);
    case SYS_ALLOC:  return sys_alloc(a0);
    case SYS_FREE:   return sys_free(a0);
    default:
        return -1;
    }
}

/* Called from assembly stub with pointer to frame */
void syscall_entry_c(struct syscall_frame *f) {
    uint64_t nr = f->rax;
    uint64_t a0 = f->rdi;
    uint64_t a1 = f->rsi;
    uint64_t a2 = f->rdx;
    uint64_t a3 = f->rcx;

    if (nr == SYS_EXIT && (f->cs & 3) == 3) {
        if (process_handle_exit(f, (int64_t)a0))
            return; /* frame rewritten → iretq to kernel */
    }

    f->rax = (uint64_t)syscall_dispatch(nr, a0, a1, a2, a3);
}

int64_t gw_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "mov %[nr], %%rax\n\t"
        "mov %[a0], %%rdi\n\t"
        "mov %[a1], %%rsi\n\t"
        "mov %[a2], %%rdx\n\t"
        "mov %[a3], %%rcx\n\t"
        "int $0x80\n\t"
        "mov %%rax, %[ret]\n\t"
        : [ret] "=r"(ret)
        : [nr] "r"(nr), [a0] "r"(a0), [a1] "r"(a1), [a2] "r"(a2), [a3] "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "memory"
    );
    return ret;
}

void syscall_init(void) {
    /* IDT gate installed from idt_init via isr stub 0x80 */
    serial_write("Syscall ABI: int 0x80 (GWKernel numbers)\n");
}
