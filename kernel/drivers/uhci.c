/*
 * Minimal UHCI + USB HID boot-protocol keyboard.
 * Enough for QEMU -device usb-kbd and many real UHCI/legacy controllers.
 */

#include <gw/usb.h>
#include <gw/kbd.h>
#include <gw/pci.h>
#include <gw/pmm.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint16_t inw(uint16_t p) {
    uint16_t r; __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(p)); return r;
}
static inline void outl(uint16_t p, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p));
}
static void delay(int n) {
    for (volatile int i = 0; i < n * 1000; i++)
        __asm__ volatile ("pause");
}

/* UHCI I/O regs */
#define USBCMD    0x00
#define USBSTS    0x02
#define USBINTR   0x04
#define FRNUM     0x06
#define FLBASE    0x08
#define SOF       0x0C
#define PORTSC1   0x10
#define PORTSC2   0x12

#define CMD_RS    0x0001
#define CMD_HCRESET 0x0002
#define CMD_GRESET  0x0004
#define CMD_MAXP  0x0080

#define PORT_CCS  0x0001
#define PORT_CSC  0x0002
#define PORT_PE   0x0004
#define PORT_PEDC 0x0008
#define PORT_RESET 0x0200

/* TD / QH */
typedef struct {
    uint32_t link;
    uint32_t cs;
    uint32_t token;
    uint32_t buffer;
} uhci_td_t;

typedef struct {
    uint32_t head_link;
    uint32_t element;
    uint32_t pad[2];
} uhci_qh_t;

static uint16_t iobase;
static int active;
static uint32_t *framelist;
static volatile uhci_qh_t *qh;
static volatile uhci_td_t *tds;
static uint8_t *ctrl_buf;
static uint8_t *report;
static uint8_t kbd_addr = 1;
static uint8_t kbd_ep = 1;
static int kbd_ready;
static int low_speed;
static volatile uhci_td_t *irq_td;

/* HID usage IDs → ASCII (boot protocol, unshifted) */
static char hid_ascii[128];
static char hid_shift[128];
static int shift, caps, ctrl_mod;

static void hid_tables(void) {
    for (int i = 0; i < 128; i++) hid_ascii[i] = hid_shift[i] = 0;
    const char *low = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;\'`,./";
    /* usage 0x04.. */
    for (int i = 0; i < 26; i++) {
        hid_ascii[0x04 + i] = (char)('a' + i);
        hid_shift[0x04 + i] = (char)('A' + i);
    }
    const char *num = "1234567890";
    for (int i = 0; i < 10; i++) {
        hid_ascii[0x1E + i] = num[i];
    }
    hid_shift[0x1E] = '!'; hid_shift[0x1F] = '@'; hid_shift[0x20] = '#';
    hid_shift[0x21] = '$'; hid_shift[0x22] = '%'; hid_shift[0x23] = '^';
    hid_shift[0x24] = '&'; hid_shift[0x25] = '*'; hid_shift[0x26] = '(';
    hid_shift[0x27] = ')';
    hid_ascii[0x28] = '\n'; /* enter */
    hid_ascii[0x29] = 0x1b; /* escape */
    hid_ascii[0x2A] = '\b';
    hid_ascii[0x2B] = '\t';
    hid_ascii[0x2C] = ' ';
    hid_ascii[0x2D] = '-'; hid_shift[0x2D] = '_';
    hid_ascii[0x2E] = '='; hid_shift[0x2E] = '+';
    hid_ascii[0x2F] = '['; hid_shift[0x2F] = '{';
    hid_ascii[0x30] = ']'; hid_shift[0x30] = '}';
    hid_ascii[0x31] = '\\'; hid_shift[0x31] = '|';
    hid_ascii[0x33] = ';'; hid_shift[0x33] = ':';
    hid_ascii[0x34] = '\''; hid_shift[0x34] = '"';
    hid_ascii[0x35] = '`'; hid_shift[0x35] = '~';
    hid_ascii[0x36] = ','; hid_shift[0x36] = '<';
    hid_ascii[0x37] = '.'; hid_shift[0x37] = '>';
    hid_ascii[0x38] = '/'; hid_shift[0x38] = '?';
    (void)low;
}

static void pci_enable_io_bm(uint8_t bus, uint8_t slot, uint8_t fn) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)(slot & 31) << 11)
        | ((uint32_t)(fn & 7) << 8) | 0x04;
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC));
    v |= 0x05; /* IO + BM */
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"((uint16_t)0xCFC));
}

static int find_uhci(uint16_t *iob) {
    for (int bus = 0; bus < 4; bus++)
    for (int slot = 0; slot < 32; slot++)
    for (int fn = 0; fn < 8; fn++) {
        uint16_t vend = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0);
        if (vend == 0xFFFF) { if (fn == 0) break; continue; }
        uint8_t classc = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0B);
        uint8_t sub = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0A);
        uint8_t prog = pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x09);
        if (classc == 0x0C && sub == 0x03 && prog == 0x00) {
            uint32_t bar = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x20);
            if (!(bar & 1)) continue; /* need IO BAR */
            *iob = (uint16_t)(bar & ~3u);
            pci_enable_io_bm((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
            serial_write("uhci: PCI ");
            serial_write_dec((uint64_t)bus);
            serial_putc(':');
            serial_write_dec((uint64_t)slot);
            serial_write(" IO=");
            serial_write_hex(*iob);
            serial_write("\n");
            return 0;
        }
        if (fn == 0 && !(pci_read8((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x0E) & 0x80))
            break;
    }
    return -1;
}

static void hc_reset(void) {
    outw(iobase + USBCMD, CMD_HCRESET);
    for (int i = 0; i < 1000; i++) {
        if (!(inw(iobase + USBCMD) & CMD_HCRESET)) break;
        delay(10);
    }
    outw(iobase + USBINTR, 0);
    outw(iobase + USBCMD, 0);
}

static int wait_td(volatile uhci_td_t *td) {
    for (int i = 0; i < 5000000; i++) {
        uint32_t cs = td->cs;
        if (!(cs & (1u << 23))) {
            /* Active cleared — check errors (ignore NAK bit 19; retried by HC) */
            if (cs & ((1u << 22) | (1u << 21) | (1u << 20) | (1u << 18) | (1u << 17))) {
                serial_write("uhci: TD err cs=");
                serial_write_hex(cs);
                serial_write("\n");
                return -1;
            }
            return 0;
        }
        if ((i & 0xFFFF) == 0) __asm__ volatile ("pause");
    }
    serial_write("uhci: TD timeout cs=");
    serial_write_hex(td->cs);
    serial_write("\n");
    return -1;
}

static int ctrl_transfer(uint8_t addr, uint8_t *setup8, uint8_t *data, int data_len, int in) {
    volatile uhci_td_t *td0 = &tds[0];
    volatile uhci_td_t *td1 = &tds[1];
    volatile uhci_td_t *td2 = &tds[2];

    /* Stop schedule while we rebuild */
    outw(iobase + USBCMD, (uint16_t)(CMD_MAXP | (1u << 6)));
    delay(5);

    for (int i = 0; i < 8; i++) {
        tds[i].link = 1;
        tds[i].cs = 0;
        tds[i].token = 0;
        tds[i].buffer = 0;
    }

    /* copy setup into DMA buffer */
    for (int i = 0; i < 8; i++) ctrl_buf[i] = setup8[i];

    uint32_t ls = low_speed ? (1u << 26) : 0;
    uint32_t act = (1u << 23) | (3u << 27) | ls;

    /* SETUP: DATA0, PID 0x2D, 8 bytes */
    td0->cs = act;
    td0->token = (7u << 21) | ((uint32_t)addr << 8) | 0x2D;
    td0->buffer = (uint32_t)(uintptr_t)ctrl_buf;

    if (data_len > 0 && data) {
        td0->link = ((uint32_t)(uintptr_t)td1) | 0x4; /* depth */
        td1->cs = act;
        td1->token = (((uint32_t)(data_len - 1)) << 21) | (1u << 19) |
                     ((uint32_t)addr << 8) | (in ? 0x69u : 0xE1u);
        td1->buffer = (uint32_t)(uintptr_t)data;
        td1->link = ((uint32_t)(uintptr_t)td2) | 0x4;
        /* STATUS opposite direction of data, DATA1, maxlen 0x7FF */
        td2->cs = act;
        td2->token = (0x7FFu << 21) | (1u << 19) | ((uint32_t)addr << 8) | (in ? 0xE1u : 0x69u);
        td2->buffer = 0;
        td2->link = 1;
    } else {
        td0->link = ((uint32_t)(uintptr_t)td2) | 0x4;
        /* no-data: STATUS is IN + DATA1 */
        td2->cs = act;
        td2->token = (0x7FFu << 21) | (1u << 19) | ((uint32_t)addr << 8) | 0x69;
        td2->buffer = 0;
        td2->link = 1;
    }

    /* QH points at SETUP TD; all frames point at QH */
    qh->head_link = 1; /* terminate horizontal */
    qh->element = (uint32_t)(uintptr_t)td0;
    for (int i = 0; i < 1024; i++)
        framelist[i] = (uint32_t)(uintptr_t)qh | 0x2;

    __asm__ volatile ("mfence" ::: "memory");
    outw(iobase + USBSTS, 0xFFFF); /* clear status */
    outw(iobase + USBCMD, (uint16_t)(CMD_RS | CMD_MAXP | (1u << 6)));

    if (wait_td(td0) != 0) return -1;
    if (data_len > 0 && wait_td(td1) != 0) return -1;
    if (wait_td(td2) != 0) return -1;

    qh->element = 1; /* empty */
    return 0;
}

static int port_enable(int port) {
    uint16_t reg = (uint16_t)(iobase + PORTSC1 + (port * 2));
    uint16_t s = inw(reg);
    if (!(s & PORT_CCS)) return -1;
    low_speed = (s & 0x100) ? 1 : 0; /* LSDA */
    /* clear change bits by writing 1 */
    outw(reg, (uint16_t)(s | PORT_CSC | PORT_PEDC));
    /* reset 50ms */
    outw(reg, PORT_RESET);
    delay(800);
    s = inw(reg);
    outw(reg, (uint16_t)(s & ~PORT_RESET));
    delay(500); /* recovery after reset */
    /* enable */
    s = inw(reg);
    outw(reg, (uint16_t)((s & ~PORT_CSC) | PORT_PE | PORT_CSC | PORT_PEDC));
    delay(300);
    s = inw(reg);
    low_speed = (s & 0x100) ? 1 : 0;
    s = inw(reg);
    if (!(s & PORT_PE)) {
        serial_write("uhci: PE not set, PORTSC=");
        serial_write_hex(s);
        serial_write("\n");
        return -1;
    }
    serial_write(low_speed ? "uhci: low-speed device\n" : "uhci: full-speed device\n");
    return 0;
}

static int setup_keyboard(void) {
    uint8_t setup[8];
    uint8_t desc[18];

    /* GET_DESCRIPTOR device (8 bytes first) — addr 0 */
    for (int i = 0; i < 18; i++) desc[i] = 0;
    setup[0] = 0x80; setup[1] = 0x06; setup[2] = 0; setup[3] = 0x01;
    setup[4] = 0; setup[5] = 0; setup[6] = 8; setup[7] = 0;
    if (ctrl_transfer(0, setup, desc, 8, 1) != 0) {
        serial_write("uhci: GET_DESCRIPTOR failed\n");
        /* continue anyway */
    } else {
        serial_write("uhci: dev desc len=");
        serial_write_dec(desc[0]);
        serial_write(" class=");
        serial_write_dec(desc[4]);
        serial_write("\n");
    }
    delay(20);

    /* SET_ADDRESS 1 */
    setup[0] = 0x00; setup[1] = 0x05; setup[2] = kbd_addr; setup[3] = 0;
    setup[4] = 0; setup[5] = 0; setup[6] = 0; setup[7] = 0;
    if (ctrl_transfer(0, setup, 0, 0, 0) != 0) {
        serial_write("uhci: SET_ADDRESS failed\n");
        return -1;
    }
    delay(100);

    /* SET_CONFIGURATION 1 */
    setup[0] = 0x00; setup[1] = 0x09; setup[2] = 1; setup[3] = 0;
    setup[4] = 0; setup[5] = 0; setup[6] = 0; setup[7] = 0;
    if (ctrl_transfer(kbd_addr, setup, 0, 0, 0) != 0) {
        serial_write("uhci: SET_CONFIGURATION failed\n");
        return -1;
    }

    /* SET_PROTOCOL boot (HID class) interface 0, protocol 0 */
    setup[0] = 0x21; setup[1] = 0x0B; setup[2] = 0; setup[3] = 0;
    setup[4] = 0; setup[5] = 0; setup[6] = 0; setup[7] = 0;
    if (ctrl_transfer(kbd_addr, setup, 0, 0, 0) != 0) {
        serial_write("uhci: SET_PROTOCOL failed (continuing)\n");
    }

    /* SET_IDLE 0 */
    setup[0] = 0x21; setup[1] = 0x0A; setup[2] = 0; setup[3] = 0;
    setup[4] = 0; setup[5] = 0; setup[6] = 0; setup[7] = 0;
    ctrl_transfer(kbd_addr, setup, 0, 0, 0);

    /* arm interrupt IN TD */
    irq_td = &tds[4];
    for (int i = 0; i < 8; i++) report[i] = 0;
    irq_td->link = 1;
    irq_td->cs = (1u << 23) | (3u << 27) | (low_speed ? (1u << 26) : 0) | (1u << 29);
    irq_td->token = (7u << 21) /* 8 bytes */ | ((uint32_t)kbd_ep << 15) |
                    ((uint32_t)kbd_addr << 8) | 0x69; /* IN */
    irq_td->buffer = (uint32_t)(uintptr_t)report;
    qh->head_link = 1;
    qh->element = (uint32_t)(uintptr_t)irq_td;
    for (int i = 0; i < 1024; i++)
        framelist[i] = (uint32_t)(uintptr_t)qh | 0x2;
    outw(iobase + USBCMD, (uint16_t)(CMD_RS | CMD_MAXP | (1u << 6)));

    kbd_ready = 1;
    serial_write("uhci: HID boot keyboard ready (addr=");
    serial_write_dec(kbd_addr);
    serial_write(")\n");
    return 0;
}

void usb_poll(void) {
    if (!active || !kbd_ready || !irq_td) return;
    if (irq_td->cs & (1u << 23)) return; /* still active */

    if (!(irq_td->cs & (1u << 22))) {
        uint8_t mods = report[0];
        shift = (mods & 0x22) ? 1 : 0;
        ctrl_mod = (mods & 0x11) ? 1 : 0;
        for (int i = 2; i < 8; i++) {
            uint8_t code = report[i];
            if (!code) continue;
            if (code == 0x39) { caps = !caps; continue; } /* caps */
            char c = 0;
            if (code < 128) {
                int up = shift ^ caps;
                c = up ? hid_shift[code] : hid_ascii[code];
                if (!c && !up) c = hid_ascii[code];
                if (!c && up) c = hid_shift[code];
                if (c >= 'a' && c <= 'z' && caps && !shift) c = (char)(c - 32);
            }
            if (ctrl_mod && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
            if (ctrl_mod && c == 'c') c = 3;
            if (c) kbd_push(c);
        }
    }

    /* re-arm */
    for (int i = 0; i < 8; i++) report[i] = 0;
    irq_td->cs = (1u << 23) | (3u << 27) | (low_speed ? (1u << 26) : 0) | (1u << 29);
    irq_td->token = (7u << 21) | ((uint32_t)kbd_ep << 15) |
                    ((uint32_t)kbd_addr << 8) | 0x69;
    irq_td->buffer = (uint32_t)(uintptr_t)report;
    irq_td->link = 1;
    qh->element = (uint32_t)(uintptr_t)irq_td;
}

void usb_init(void) {
    active = 0;
    kbd_ready = 0;
    hid_tables();

    if (find_uhci(&iobase) != 0) {
        serial_write("uhci: no UHCI controller\n");
        return;
    }

    uint64_t page = pmm_alloc_pages(3);
    if (!page) {
        serial_write("uhci: OOM\n");
        return;
    }
    uint8_t *mem = (uint8_t *)(uintptr_t)page;
    for (int i = 0; i < 4096 * 3; i++) mem[i] = 0;

    framelist = (uint32_t *)mem;
    qh = (volatile uhci_qh_t *)(mem + 4096);
    tds = (volatile uhci_td_t *)(mem + 4096 + 64);
    ctrl_buf = mem + 4096 + 512;
    report = mem + 4096 + 520;

    qh->head_link = 1;
    qh->element = 1;

    for (int i = 0; i < 1024; i++)
        framelist[i] = (uint32_t)(uintptr_t)qh | 0x2; /* QH */

    hc_reset();
    outl(iobase + FLBASE, (uint32_t)(uintptr_t)framelist);
    outw(iobase + FRNUM, 0);
    outw(iobase + USBINTR, 0);
    outw(iobase + USBCMD, (uint16_t)(CMD_RS | CMD_MAXP | (1u << 6))); /* RS+MAXP+CF */
    delay(50);
    serial_write("uhci: USBSTS=");
    serial_write_hex(inw(iobase + USBSTS));
    serial_write(" USBCMD=");
    serial_write_hex(inw(iobase + USBCMD));
    serial_write("\n");
    active = 1;

    /* wait for connect on port 0 or 1 */
    int port = -1;
    for (int t = 0; t < 50; t++) {
        for (int p = 0; p < 2; p++) {
            uint16_t s = inw((uint16_t)(iobase + PORTSC1 + p * 2));
            if (s & PORT_CCS) { port = p; break; }
        }
        if (port >= 0) break;
        delay(100);
    }
    if (port < 0) {
        serial_write("uhci: no device on root ports\n");
        return;
    }
    serial_write("uhci: device on port ");
    serial_write_dec((uint64_t)port);
    serial_write("\n");

    if (port_enable(port) != 0) {
        serial_write("uhci: port enable failed\n");
        return;
    }
    setup_keyboard();
}
