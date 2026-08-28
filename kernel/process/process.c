/*
 * Ring-3 process entry / ELF exec (VFS + FAT)
 */

#include <gw/process.h>
#include <gw/elf.h>
#include <gw/gdt.h>
#include <gw/vfs.h>
#include <gw/fat.h>
#include <gw/heap.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

extern char user_program_start[];
extern char user_program_end[];
extern char _binary_hello_elf_start[];
extern char _binary_hello_elf_end[];

#define USER_STACK_TOP  0x403000ULL
#define USER_CODE_FALLBACK 0x400000ULL
#define EXEC_BUF_MAX (256 * 1024)

static process_t current_proc;
static int next_pid = 1;
static uint8_t user_kstack[16384] __attribute__((aligned(16)));
static volatile int64_t last_exit_code;

process_t *process_current(void) {
    return current_proc.active ? &current_proc : 0;
}

static void memcpy_l(void *d, const void *s, uint64_t n) {
    uint8_t *dd = d;
    const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
}

struct irq_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

extern void process_switch_to_user(uint64_t entry, uint64_t user_rsp, uint64_t *saved_krsp);
extern void process_user_resume(void);

int process_handle_exit(void *frame_ptr, int64_t code) {
    struct irq_frame *f = frame_ptr;
    if (!current_proc.active)
        return 0;

    current_proc.exit_code = (int)code;
    last_exit_code = code;
    current_proc.active = 0;

    serial_write("process: user exit code=");
    serial_write_dec((uint64_t)(uint32_t)code);
    serial_write(" — returning to kernel\n");

    f->rip = (uint64_t)process_user_resume;
    f->cs = 0x08;
    f->rflags = 0x202;
    f->rsp = current_proc.kernel_rsp;
    f->ss = 0x10;
    f->rax = (uint64_t)code;
    return 1;
}

static int enter_at(uint64_t entry) {
    uint8_t *stk = (uint8_t *)(uintptr_t)(USER_STACK_TOP - 0x1000);
    for (int i = 0; i < 0x1000; i++) stk[i] = 0;

    tss_set_kernel_stack((uint64_t)(user_kstack + sizeof(user_kstack)));
    current_proc.active = 1;
    current_proc.exit_code = -1;
    last_exit_code = -1;

    if (!current_proc.pid)
        current_proc.pid = next_pid++;
    serial_write("process: pid=");
    serial_write_dec((uint64_t)current_proc.pid);
    serial_write(" iretq -> ring 3 @ ");
    serial_write_hex(entry);
    serial_write("\n");

    process_switch_to_user(entry, USER_STACK_TOP, &current_proc.kernel_rsp);

    serial_write("process: back in kernel, exit=");
    serial_write_dec((uint64_t)(uint32_t)last_exit_code);
    serial_write("\n");
    return (int)last_exit_code;
}

int user_enter(void) {
    uint64_t size = (uint64_t)(user_program_end - user_program_start);
    serial_write("process: raw blob (");
    serial_write_dec(size);
    serial_write(" bytes)\n");
    memcpy_l((void *)(uintptr_t)USER_CODE_FALLBACK, user_program_start, size);
    current_proc.pid = next_pid++;
    return enter_at(USER_CODE_FALLBACK);
}

int process_list(void) {
    serial_write("PID  STATE  NAME\n");
    if (current_proc.active) {
        serial_write_dec((uint64_t)current_proc.pid);
        serial_write("  run    ");
        serial_write(current_proc.name[0] ? current_proc.name : "?");
        serial_write("\n");
    } else {
        serial_write("(no user process)\n");
    }
    return 0;
}

static void set_name(const char *path) {
    int i = 0, j = 0;
    const char *s = path;
    for (int k = 0; path[k]; k++)
        if (path[k] == '/' || path[k] == '\\') s = path + k + 1;
    while (s[i] && j < 31) current_proc.name[j++] = s[i++];
    current_proc.name[j] = 0;
}

static int load_and_run(const void *data, size_t n, const char *src) {
    serial_write("exec: loaded ");
    serial_write_dec(n);
    serial_write(" bytes from ");
    serial_write(src);
    serial_write("\n");
    elf_info_t info;
    if (elf_load(data, n, &info) != 0)
        return -1;
    return enter_at(info.entry);
}

int process_exec(const char *path) {
    uint8_t *buf = kmalloc(EXEC_BUF_MAX);
    if (!buf) {
        serial_write("exec: OOM\n");
        return -1;
    }

    set_name(path);
    current_proc.pid = next_pid++;
    size_t n = 0;
    /* 1) RAMFS / VFS */
    if (vfs_read(path, buf, EXEC_BUF_MAX - 1, &n) >= 0 && n > 0) {
        int rc = load_and_run(buf, n, "VFS");
        kfree(buf);
        return rc;
    }

    /* 2) FAT ESP (8.3 name) */
    n = 0;
    if (fat_read_file(path, buf, EXEC_BUF_MAX, &n) >= 0 && n > 0) {
        int rc = load_and_run(buf, n, "FAT");
        kfree(buf);
        return rc;
    }

    serial_write("exec: not found: ");
    serial_write(path);
    serial_write("\n");
    kfree(buf);
    return -1;
}

void process_install_hello_elf(void) {
    size_t sz = (size_t)(_binary_hello_elf_end - _binary_hello_elf_start);
    if (sz == 0) {
        serial_write("exec: no embedded hello.elf\n");
        return;
    }
    if (vfs_write("C:/Users/root/hello.elf", _binary_hello_elf_start, sz) < 0)
        serial_write("exec: failed to install hello.elf into RAMFS\n");
    else {
        serial_write("exec: RAMFS C:/Users/root/hello.elf (");
        serial_write_dec(sz);
        serial_write(" bytes)\n");
    }
}
