/*
 * CMOS RTC (MC146818) — ports 0x70/0x71
 */

#include <gw/rtc.h>
#include <gw/serial.h>
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, (uint8_t)(reg | 0x80)); /* NMI off during read */
    return inb(0x71);
}

static int bcd_to_bin(uint8_t v) {
    return (int)((v & 0x0F) + ((v >> 4) * 10));
}

void rtc_init(void) {
    serial_write("rtc: CMOS ready\n");
}

int rtc_read(rtc_time_t *out) {
    if (!out) return -1;
    /* wait update-in-progress clear */
    for (int i = 0; i < 10000; i++) {
        if (!(cmos_read(0x0A) & 0x80)) break;
    }
    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hr  = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t mon = cmos_read(0x08);
    uint8_t yr  = cmos_read(0x09);
    uint8_t cent = cmos_read(0x32); /* may be 0 on some */
    uint8_t regb = cmos_read(0x0B);

    int binary = (regb & 0x04) != 0;
    int h24 = (regb & 0x02) != 0;

    if (!binary) {
        sec = (uint8_t)bcd_to_bin(sec);
        min = (uint8_t)bcd_to_bin(min);
        hr  = (uint8_t)bcd_to_bin(hr & 0x7F);
        day = (uint8_t)bcd_to_bin(day);
        mon = (uint8_t)bcd_to_bin(mon);
        yr  = (uint8_t)bcd_to_bin(yr);
        if (cent) cent = (uint8_t)bcd_to_bin(cent);
    } else {
        hr = (uint8_t)(hr & 0x7F);
    }

    if (!h24) {
        /* 12-hour: bit 7 of raw was PM — approximate if needed */
        if (hr == 12) hr = 0;
    }

    int year;
    if (cent)
        year = (int)cent * 100 + (int)yr;
    else
        year = 2000 + (int)yr;

    out->year = year;
    out->month = (int)mon;
    out->day = (int)day;
    out->hour = (int)hr;
    out->minute = (int)min;
    out->second = (int)sec;
    return 0;
}
