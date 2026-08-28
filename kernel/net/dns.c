/*
 * Minimal DNS A-record client (QEMU user-net DNS = 10.0.2.3)
 */

#include <gw/dns.h>
#include <gw/udp.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* Parse dotted quad; return 1 if name is pure IP */
static int parse_ip(const char *name, uint8_t out[4]) {
    int parts[4] = {0,0,0,0}, pi = 0;
    const char *p = name;
    if (!p || !*p) return 0;
    while (*p && pi < 4) {
        if (!is_digit(*p)) return 0;
        int v = 0;
        while (is_digit(*p)) {
            v = v * 10 + (*p - '0');
            if (v > 255) return 0;
            p++;
        }
        parts[pi++] = v;
        if (*p == '.') { p++; continue; }
        break;
    }
    if (pi != 4 || *p) return 0;
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)parts[i];
    return 1;
}


int dns_resolve(const char *name, uint8_t out_ip[4]) {
    if (!name || !out_ip) return -1;
    if (parse_ip(name, out_ip))
        return 0;

    /* Build DNS query */
    uint8_t q[512];
    size_t n = 0;
    static uint16_t txid = 0x1234;
    txid++;
    q[n++] = (uint8_t)(txid >> 8); q[n++] = (uint8_t)txid;
    q[n++] = 0x01; q[n++] = 0x00; /* standard query, recursion desired */
    q[n++] = 0x00; q[n++] = 0x01; /* 1 question */
    q[n++] = 0x00; q[n++] = 0x00;
    q[n++] = 0x00; q[n++] = 0x00;
    q[n++] = 0x00; q[n++] = 0x00;

    /* QNAME */
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t lab = (size_t)(dot - p);
        if (lab == 0 || lab > 63) return -1;
        q[n++] = (uint8_t)lab;
        for (size_t i = 0; i < lab; i++) q[n++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    q[n++] = 0;
    q[n++] = 0x00; q[n++] = 0x01; /* A */
    q[n++] = 0x00; q[n++] = 0x01; /* IN */

    uint8_t dns_ip[4] = {10, 0, 2, 3};
    uint16_t local = 53000 + (txid & 0xFF);
    if (udp_send(dns_ip, 53, local, q, n) != 0) {
        serial_write("dns: send failed\n");
        return -1;
    }

    uint8_t resp[512];
    size_t rlen = 0;
    uint8_t from[4];
    uint16_t fport = 0;
    if (udp_recv(local, resp, sizeof(resp), &rlen, from, &fport, 40) != 0) {
        serial_write("dns: timeout\n");
        return -1;
    }
    if (rlen < 12) return -1;

    /* Answers */
    uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    if (ancount == 0) {
        serial_write("dns: no answers\n");
        return -1;
    }

    /* Skip question section */
    size_t i = 12;
    while (i < rlen && resp[i] != 0) {
        if ((resp[i] & 0xC0) == 0xC0) { i += 2; break; }
        i += (size_t)resp[i] + 1;
    }
    if (i < rlen && resp[i] == 0) i++;
    i += 4; /* type + class */

    for (uint16_t a = 0; a < ancount && i + 12 <= rlen; a++) {
        if ((resp[i] & 0xC0) == 0xC0) i += 2;
        else {
            while (i < rlen && resp[i] != 0) {
                if ((resp[i] & 0xC0) == 0xC0) { i += 2; break; }
                i += (size_t)resp[i] + 1;
            }
            if (i < rlen && resp[i] == 0) i++;
        }
        if (i + 10 > rlen) break;
        uint16_t atype = (uint16_t)((resp[i] << 8) | resp[i + 1]);
        uint16_t rdlen = (uint16_t)((resp[i + 8] << 8) | resp[i + 9]);
        i += 10;
        if (atype == 1 && rdlen == 4 && i + 4 <= rlen) {
            out_ip[0] = resp[i];
            out_ip[1] = resp[i + 1];
            out_ip[2] = resp[i + 2];
            out_ip[3] = resp[i + 3];
            serial_write("dns: ");
            serial_write(name);
            serial_write(" -> ");
            serial_write_dec(out_ip[0]); serial_putc('.');
            serial_write_dec(out_ip[1]); serial_putc('.');
            serial_write_dec(out_ip[2]); serial_putc('.');
            serial_write_dec(out_ip[3]);
            serial_write("\n");
            return 0;
        }
        i += rdlen;
    }
    serial_write("dns: no A record\n");
    return -1;
}
