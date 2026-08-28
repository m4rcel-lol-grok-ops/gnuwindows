/*
 * PS/2 keyboard — IRQ1, scancode set 1, US layout (make codes only)
 */

#include <gw/kbd.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define KBD_DATA 0x60
#define KBD_STAT 0x64

static inline uint8_t inb(uint16_t p) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r;
}

#define KBUF 64
static char buf[KBUF];
static volatile int head, tail;
static int shift, caps, ctrl;

/* Set-1 make codes → ASCII (unshifted / shifted) */
static const char map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char map_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

static void push(char c) {
    int n = (head + 1) % KBUF;
    if (n == tail) return; /* full */
    buf[head] = c;
    head = n;
}

void kbd_irq(void) {
    uint8_t st = inb(KBD_STAT);
    if (!(st & 1)) return;
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) return; /* ignore extended prefix for now */

    int release = sc & 0x80;
    sc &= 0x7F;

    if (sc == 0x2A || sc == 0x36) { /* L/R shift */
        shift = release ? 0 : 1;
        return;
    }
    if (sc == 0x1D) { /* ctrl */
        ctrl = release ? 0 : 1;
        return;
    }
    if (sc == 0x3A && !release) {
        caps = !caps;
        return;
    }
    if (release) return;

    if (ctrl && sc == 0x2E) { /* Ctrl+C */
        push(0x03);
        return;
    }

    char c = 0;
    if (sc < 128) {
        int up = shift ^ caps;
        c = up ? map_shift[sc] : map[sc];
        if (c >= 'a' && c <= 'z' && caps && !shift) c -= 32;
        if (c >= 'A' && c <= 'Z' && caps && shift) c += 32;
    }
    if (c) push(c);
}

int kbd_getc_nonblock(void) {
    if (head == tail) return -1;
    char c = buf[tail];
    tail = (tail + 1) % KBUF;
    return (int)(uint8_t)c;
}

void kbd_init(void) {
    head = tail = 0;
    shift = caps = ctrl = 0;
    /* drain */
    while (inb(KBD_STAT) & 1)
        (void)inb(KBD_DATA);
    serial_write("kbd: PS/2 IRQ1 ready\n");
}
