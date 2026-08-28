#include <gw/boot_info.h>
#include <gw/serial.h>
#include <gw/cpu.h>
#include <gw/pmm.h>
#include <gw/heap.h>
#include <gw/timer.h>
#include <gw/sched.h>
#include <gw/thread.h>
#include <gw/console.h>
#include <gw/vfs.h>
#include <gw/syscall.h>
#include <gw/vmm.h>
#include <gw/fb.h>
#include <gw/process.h>
#include <gw/fat.h>
#include <gw/pkg.h>
#include <gw/net.h>
#include <gw/dns.h>
#include <gw/kbd.h>
#include <gw/pic.h>
#include <gw/rtc.h>
#include <gw/pci.h>
#include <stdint.h>
#include <stddef.h>

static void bg_idle(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile ("pause");
        thread_yield();
    }
}

void kmain(GW_BOOT_INFO *boot_info) {
    serial_init();

    serial_write("\n========================================\n");
    serial_write("  GNU/Windows  |  GWKernel\n");
    serial_write("========================================\n");
    serial_write("Custom kernel — Linux NOT PRESENT\n\n");

    if (!boot_info || boot_info->magic != GW_BOOT_MAGIC) {
        serial_write("ERROR: boot_info\n");
        for (;;) __asm__ volatile ("hlt");
    }

    cpu_init();
    serial_write("\n");
    if (pmm_init(boot_info) != 0) {
        serial_write("FATAL: PMM\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    serial_write("\n");
    if (heap_init() != 0) {
        serial_write("FATAL: heap\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    serial_write("\n");
    if (vmm_init() != 0) {
        serial_write("FATAL: VMM\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    vmm_load();
    fb_init(boot_info);
    serial_write("\n");
    if (vfs_init() != 0) {
        serial_write("FATAL: VFS\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    process_install_hello_elf();
    serial_write("\n");
    fat_mount();
    serial_write("\n");
    syscall_init();
    serial_write("\n");

    /* Disk exec self-test before scheduler (runs in kmain context) */
    serial_write("--- boot self-test: fat mkdir/write/read ---\n");
    if (fat_mkdir("APPS") != 0)
        serial_write("mkdir APPS failed\n");
    {
        const char *msg = "hello from APPS/NOTE.TXT\n";
        size_t n = 0;
        while (msg[n]) n++;
        if (fat_write_file("APPS/NOTE.TXT", msg, n) < 0)
            serial_write("write APPS/NOTE.TXT failed\n");
        else {
            char buf[128];
            size_t got = 0;
            if (fat_read_file("APPS/NOTE.TXT", buf, sizeof(buf) - 1, &got) < 0)
                serial_write("readback failed\n");
            else {
                buf[got] = 0;
                serial_write("readback: ");
                serial_write(buf);
            }
        }
    }
    serial_write("--- fatls / ---\n");
    fat_list("/");
    serial_write("--- fatls APPS ---\n");
    fat_list("APPS");
    serial_write("--- boot self-test: exec HELLO.ELF from FAT ---\n");
    process_exec("HELLO.ELF");
    serial_write("--- unlink test ---\n");
    fat_write_file("APPS/TMP.TXT", "tmp\n", 4);
    fat_unlink("APPS/TMP.TXT");
    fat_list("APPS");
    serial_write("--- VFS D: drive test ---\n");
    vfs_chdir("D:/");
    vfs_list("");
    {
        char buf[128];
        size_t got = 0;
        if (vfs_read("APPS/NOTE.TXT", buf, sizeof(buf)-1, &got) >= 0) {
            buf[got] = 0;
            serial_write("VFS read D:APPS/NOTE.TXT: ");
            serial_write(buf);
        }
    }
    vfs_chdir("C:/Users/root");
    serial_write("--- gwpkg (repo model) test ---\n");
    net_init();
    net_status();
    {
        uint8_t ip[4];
        serial_write("--- DNS test ---\n");
        if (dns_resolve("10.0.2.100", ip) == 0)
            serial_write("dns: dotted IP parse OK\n");
        /* skip unknown-name probe (UDP timeout is slow on boot) */
    }
    pkg_init();
    serial_write("--- gwpkg HTTP .pkg fetch ---\n");
    pkg_fetch("hello");
    pkg_info("hello");
    pkg_install("hello");
    vfs_chdir("C:/Program Files/HELLO");
    vfs_list("");
    {
        char b[128]; size_t n = 0;
        if (vfs_read("README.TXT", b, sizeof(b)-1, &n) >= 0) {
            b[n] = 0;
            serial_write("installed readme: ");
            serial_write(b);
        }
    }
    vfs_chdir("C:/Users/root");
    serial_write("--- self-test done ---\n\n");

    sched_init();
    thread_create("console", console_thread, NULL);
    thread_create("bg", bg_idle, NULL);
    timer_init();
    kbd_init();
    pic_unmask(1);
    rtc_init();
    pci_init();
    pci_list();
    {
        rtc_time_t tm;
        if (rtc_read(&tm) == 0) {
            serial_write("date: ");
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
            serial_write("\n");
        }
    }
    cpu_enable_interrupts();
    serial_write("Ready: shell, RAMFS, FAT ESP, exec, ring3\n");
    sched_start();
}
