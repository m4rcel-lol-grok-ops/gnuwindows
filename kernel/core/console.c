/*
 * Interactive serial console — VFS-aware shell
 */

#include <gw/console.h>
#include <gw/kbd.h>
#include <gw/serial.h>
#include <gw/timer.h>
#include <gw/pmm.h>
#include <gw/heap.h>
#include <gw/thread.h>
#include <gw/cpu.h>
#include <gw/vfs.h>
#include <gw/process.h>
/* process_list */
#include <gw/fat.h>
#include <gw/pkg.h>
#include <gw/net.h>
#include <gw/dns.h>
#include <gw/rtc.h>
#include <gw/pci.h>
#include <stdint.h>
#include <stddef.h>

#define LINE_MAX 128

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_starts(const char *s, const char *pfx) {
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static const char *first_arg(const char *p) {
    skip_ws(&p);
    while (*p && *p != ' ' && *p != '\t') p++;
    skip_ws(&p);
    return p;
}

static void cmd_help(void) {
    serial_write("GNU/Windows console\n");
    serial_write("  help, uname, date, uptime, ticks, mem, threads, yield, halt\n");
    serial_write("  echo <text>\n");
    serial_write("  pwd / cd <path>   (C: RAMFS, D: FAT disk)\n");
    serial_write("  dir / ls [path]\n");
    serial_write("  mkdir <path>\n");
    serial_write("  type / cat <file>\n");
    serial_write("  write <file> <text>\n");
    serial_write("  del / rm <path>\n");
    serial_write("  runuser / exec <file>\n");
    serial_write("  fatls / fatcat / fatwrite / fatmkdir / fatcd\n");
    serial_write("  net / ping / dns <host>\n");
    serial_write("  pkg list|info|install|fetch|repo\n");
    serial_write("  lspci — PCI device list\n");
}

static void print_cwd(void) {
    char buf[160];
    vfs_getcwd(buf, sizeof(buf));
    serial_write(buf);
}

static void handle_line(char *line) {
    const char *p = line;
    skip_ws(&p);
    if (!*p) return;

    if (streq(p, "help") || streq(p, "?")) {
        cmd_help();
    } else if (streq(p, "uname")) {
        serial_write("GNU/Windows\nKernel: GWKernel (custom)\nArch: x86_64\nBoot: UEFI\nLinux: NOT PRESENT\n");
    } else if (streq(p, "ticks")) {
        serial_write("ticks=");
        serial_write_dec(timer_ticks());
        serial_write("\n");
    } else if (streq(p, "uptime")) {
        uint64_t t = timer_ticks();
        uint64_t sec = t / 100; /* PIT ~100 Hz */
        uint64_t h = sec / 3600;
        uint64_t m = (sec / 60) % 60;
        uint64_t s = sec % 60;
        serial_write("uptime ");
        serial_write_dec(h);
        serial_write("h ");
        serial_write_dec(m);
        serial_write("m ");
        serial_write_dec(s);
        serial_write("s (");
        serial_write_dec(t);
        serial_write(" ticks)\n");
    } else if (streq(p, "date") || streq(p, "time")) {
        rtc_time_t tm;
        if (rtc_read(&tm) != 0) serial_write("date: rtc read failed\n");
        else {
            /* YYYY-MM-DD HH:MM:SS */
            serial_write_dec((uint64_t)tm.year);
            serial_putc('-');
            if (tm.month < 10) serial_putc('0');
            serial_write_dec((uint64_t)tm.month);
            serial_putc('-');
            if (tm.day < 10) serial_putc('0');
            serial_write_dec((uint64_t)tm.day);
            serial_putc(' ');
            if (tm.hour < 10) serial_putc('0');
            serial_write_dec((uint64_t)tm.hour);
            serial_putc(':');
            if (tm.minute < 10) serial_putc('0');
            serial_write_dec((uint64_t)tm.minute);
            serial_putc(':');
            if (tm.second < 10) serial_putc('0');
            serial_write_dec((uint64_t)tm.second);
            serial_write(" (CMOS RTC)\n");
        }
    } else if (streq(p, "mem") || streq(p, "free")) {
        serial_write("PMM free pages: ");
        serial_write_dec(pmm_get_free_pages());
        serial_write("\nHeap used=");
        serial_write_dec(heap_used_bytes());
        serial_write(" free~=");
        serial_write_dec(heap_free_bytes());
        serial_write("\n");
    } else if (str_starts(p, "dns ") || streq(p, "nslookup") || str_starts(p, "nslookup ")) {
        const char *arg = first_arg(p);
        if (!*arg) serial_write("dns <hostname>\n");
        else {
            uint8_t ip[4];
            if (dns_resolve(arg, ip) != 0) serial_write("dns: failed\n");
        }
    } else if (streq(p, "lspci") || streq(p, "pci")) {
        pci_list();
    } else if (streq(p, "net") || streq(p, "ifconfig")) {
        net_status();
    } else if (streq(p, "ping") || str_starts(p, "ping ")) {
        /* ping 10.0.2.2 gateway */
        net_ping(10, 0, 2, 2);
    } else if (streq(p, "pkg") || str_starts(p, "pkg ")) {
        const char *arg = first_arg(p);
        if (!*arg || streq(arg, "list")) pkg_list();
        else if (str_starts(arg, "repo")) {
            const char *r = arg + 4;
            while (*r == ' ') r++;
            if (!*r || streq(r, "list")) pkg_repo_list();
            else if (str_starts(r, "add ")) pkg_repo_add(r + 4);
            else serial_write("pkg repo list|add <url>\n");
        } else if (str_starts(arg, "info")) {
            const char *n = arg + 4;
            while (*n == ' ') n++;
            if (!*n) serial_write("pkg info <name>\n");
            else pkg_info(n);
        } else if (str_starts(arg, "fetch")) {
            const char *n = arg + 5;
            while (*n == ' ') n++;
            if (!*n) serial_write("pkg fetch <name>\n");
            else pkg_fetch(n);
        } else if (str_starts(arg, "install")) {
            const char *n = arg + 7;
            while (*n == ' ') n++;
            if (!*n) serial_write("pkg install <name>\n");
            else pkg_install(n);
        } else serial_write("pkg list|info|fetch|install|repo\n");
    } else if (streq(p, "ps") || streq(p, "processes")) {
        process_list();
    } else if (streq(p, "threads")) {
        thread_t *cur = thread_current();
        serial_write("RR kernel threads; current=");
        if (cur) serial_write(cur->name);
        serial_write("\n");
    } else if (str_starts(p, "echo")) {
        serial_write(first_arg(p));
        serial_write("\n");
    } else if (streq(p, "clear") || streq(p, "cls")) {
        for (int i = 0; i < 40; i++) serial_putc('\n');
    } else if (streq(p, "yield")) {
        thread_yield();
        serial_write("ok\n");
    } else if (streq(p, "halt")) {
        serial_write("Halting.\n");
        cpu_disable_interrupts();
        for (;;) __asm__ volatile ("hlt");
    } else if (streq(p, "pwd")) {
        print_cwd();
        serial_write("\n");
    } else if (str_starts(p, "cd")) {
        const char *arg = first_arg(p);
        if (!*arg) { print_cwd(); serial_write("\n"); return; }
        if (vfs_chdir(arg) != 0) serial_write("cd: not a directory\n");
    } else if (streq(p, "dir") || streq(p, "ls") || str_starts(p, "dir ") || str_starts(p, "ls ")) {
        const char *arg = first_arg(p);
        vfs_list(*arg ? arg : "");
    } else if (str_starts(p, "mkdir")) {
        const char *arg = first_arg(p);
        if (!*arg) serial_write("mkdir: need path\n");
        else if (vfs_mkdir(arg) != 0) serial_write("mkdir: failed\n");
        else serial_write("ok\n");
    } else if (str_starts(p, "type") || str_starts(p, "cat")) {
        const char *arg = first_arg(p);
        if (!*arg) { serial_write("need file\n"); return; }
        char buf[512];
        size_t n = 0;
        if (vfs_read(arg, buf, sizeof(buf) - 1, &n) < 0)
            serial_write("cat: not found\n");
        else {
            buf[n] = 0;
            serial_write(buf);
            if (n == 0 || buf[n - 1] != '\n') serial_write("\n");
        }
    } else if (str_starts(p, "write")) {
        const char *arg = first_arg(p);
        if (!*arg) { serial_write("write <file> <text>\n"); return; }
        char name[VFS_NAME_MAX];
        size_t i = 0;
        while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && i + 1 < VFS_NAME_MAX) {
            name[i] = arg[i];
            i++;
        }
        name[i] = 0;
        const char *text = arg + i;
        skip_ws(&text);
        if (vfs_write(name, text, 0) < 0 && !text[0]) {
            /* create empty via write of empty — fix length */
        }
        size_t len = 0;
        while (text[len]) len++;
        if (vfs_write(name, text, len) < 0)
            serial_write("write: failed\n");
        else serial_write("ok\n");
    } else if (streq(p, "runuser") || streq(p, "user")) {
        serial_write("Launching embedded ring-3 program\n");
        user_enter();
    } else if (str_starts(p, "fatwrite")) {
        const char *arg = first_arg(p);
        if (!*arg) { serial_write("fatwrite <NAME.EXT> <text>\n"); }
        else {
            char fname[64];
            size_t i = 0;
            while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && i + 1 < sizeof(fname)) {
                fname[i] = arg[i];
                i++;
            }
            fname[i] = 0;
            const char *text = arg + i;
            while (*text == ' ' || *text == '\t') text++;
            size_t len = 0;
            while (text[len]) len++;
            if (fat_write_file(fname, text, len) < 0)
                serial_write("fatwrite: failed\n");
        }
    } else if (streq(p, "fatls") || streq(p, "fdir") || str_starts(p, "fatls ") || str_starts(p, "fdir ")) {
        const char *arg = first_arg(p);
        fat_list(*arg ? arg : "");
    } else if (str_starts(p, "fatcat")) {
        const char *arg = first_arg(p);
        if (!*arg) { serial_write("fatcat <NAME.EXT>\n"); }
        else {
            char buf[513];
            size_t n = 0;
            if (fat_read_file(arg, buf, 512, &n) < 0)
                serial_write("fatcat: not found\n");
            else {
                serial_write("read ");
                serial_write_dec(n);
                serial_write(" bytes. head: ");
                for (size_t i = 0; i < n && i < 16; i++) {
                    unsigned char c = (unsigned char)buf[i];
                    const char *h = "0123456789ABCDEF";
                    serial_putc(h[c >> 4]);
                    serial_putc(h[c & 0xF]);
                    serial_putc(' ');
                }
                serial_write("\n");
            }
        }
    } else if (str_starts(p, "exec")) {
        const char *arg = first_arg(p);
        if (!*arg) serial_write("exec <elf>\n");
        else {
            serial_write("exec ");
            serial_write(arg);
            serial_write("\n");
            process_exec(arg);
        }
    } else if (str_starts(p, "del") || str_starts(p, "rm")) {
        const char *arg = first_arg(p);
        if (!*arg) serial_write("need path\n");
        else if (vfs_unlink(arg) != 0) serial_write("rm: failed\n");
        else serial_write("ok\n");
    } else {
        serial_write("unknown: ");
        serial_write(p);
        serial_write(" (help)\n");
    }
}

void console_thread(void *arg) {
    (void)arg;
    char line[LINE_MAX];
    int len = 0;

    while (serial_getc_nonblock() >= 0)
        ;
    while (kbd_getc_nonblock() >= 0)
        ;

    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  root@GNUWIN ");
    print_cwd();
    serial_write("\n  Type 'help'. Filesystem: RAMFS on C:/\n");
    serial_write("========================================\n");

    for (;;) {
        serial_write("root@GNUWIN ");
        print_cwd();
        serial_write(" @ ");
        len = 0;
        for (;;) {
            int ch;
            while ((ch = serial_getc_nonblock()) < 0 && (ch = kbd_getc_nonblock()) < 0)
                thread_yield();
            char c = (char)ch;
            if (c == '\r' || c == '\n') {
                serial_write("\n");
                line[len] = 0;
                handle_line(line);
                break;
            }
            if (c == 0x7F || c == 0x08) {
                if (len > 0) {
                    len--;
                    serial_write("\b \b");
                }
                continue;
            }
            if (c == 0x03) {
                serial_write("^C\n");
                len = 0;
                break;
            }
            if (c >= 32 && c < 127 && len + 1 < LINE_MAX) {
                line[len++] = c;
                serial_putc(c);
            }
        }
    }
}
