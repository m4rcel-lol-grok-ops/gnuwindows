/*
 * Minimal single-connection TCP client.
 * One active connection; close fully before the next connect.
 */

#include <gw/tcp.h>
#include <gw/net.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

enum { ST_CLOSED = 0, ST_SYN_SENT, ST_EST, ST_CLOSE_WAIT };

static int state;
static uint8_t remote_ip[4];
static uint16_t local_port, remote_port;
static uint32_t snd_nxt, rcv_nxt;
static uint8_t rx_data[8192];
static size_t rx_len;
static int got_synack, got_fin;
static uint32_t isn_seed = 0xC0FFEE;

static void memcpy_l(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++;
}
static void memset_l(void *d, int c, size_t n) {
    uint8_t *p = d; while (n--) *p++ = (uint8_t)c;
}

static void settle(int n) {
    if (n > 30) n = 30;
    for (int i = 0; i < n; i++) {
        net_poll();
        for (volatile int x = 0; x < 4000; x++) {}
    }
}

static uint16_t tcp_checksum(const uint8_t src[4], const uint8_t dst[4],
                             const uint8_t *tcp, size_t tcp_len) {
    uint32_t sum = 0;
    sum += (uint32_t)((src[0] << 8) | src[1]);
    sum += (uint32_t)((src[2] << 8) | src[3]);
    sum += (uint32_t)((dst[0] << 8) | dst[1]);
    sum += (uint32_t)((dst[2] << 8) | dst[3]);
    sum += 6;
    sum += (uint32_t)tcp_len;
    for (size_t i = 0; i + 1 < tcp_len; i += 2)
        sum += (uint32_t)((tcp[i] << 8) | tcp[i + 1]);
    if (tcp_len & 1)
        sum += (uint32_t)(tcp[tcp_len - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int send_segment(uint8_t flags, const void *data, size_t len, uint32_t ack) {
    uint8_t seg[1400];
    size_t hdr = 20;
    size_t total = hdr + len;
    if (total > sizeof(seg)) return -1;
    memset_l(seg, 0, hdr);
    seg[0] = (uint8_t)(local_port >> 8);
    seg[1] = (uint8_t)local_port;
    seg[2] = (uint8_t)(remote_port >> 8);
    seg[3] = (uint8_t)remote_port;
    seg[4] = (uint8_t)(snd_nxt >> 24);
    seg[5] = (uint8_t)(snd_nxt >> 16);
    seg[6] = (uint8_t)(snd_nxt >> 8);
    seg[7] = (uint8_t)snd_nxt;
    seg[8] = (uint8_t)(ack >> 24);
    seg[9] = (uint8_t)(ack >> 16);
    seg[10] = (uint8_t)(ack >> 8);
    seg[11] = (uint8_t)ack;
    seg[12] = 0x50;
    seg[13] = flags;
    seg[14] = 0x40;
    seg[15] = 0x00;
    if (data && len)
        memcpy_l(seg + hdr, data, len);
    const uint8_t *sip = net_ip_addr();
    uint16_t c = tcp_checksum(sip, remote_ip, seg, total);
    seg[16] = (uint8_t)(c >> 8);
    seg[17] = (uint8_t)c;
    return net_ipv4_send(remote_ip, 6, seg, total);
}

static void on_tcp(const uint8_t *src_ip, const uint8_t *tcp, size_t len) {
    if (len < 20) return;
    if (src_ip[0] != remote_ip[0] || src_ip[1] != remote_ip[1] ||
        src_ip[2] != remote_ip[2] || src_ip[3] != remote_ip[3])
        return;

    uint16_t sport = (uint16_t)((tcp[0] << 8) | tcp[1]);
    uint16_t dport = (uint16_t)((tcp[2] << 8) | tcp[3]);
    if (dport != local_port) return;

    uint32_t seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                   ((uint32_t)tcp[6] << 8) | (uint32_t)tcp[7];
    uint32_t ack = ((uint32_t)tcp[8] << 24) | ((uint32_t)tcp[9] << 16) |
                   ((uint32_t)tcp[10] << 8) | (uint32_t)tcp[11];
    uint8_t doff = (uint8_t)((tcp[12] >> 4) * 4);
    uint8_t flags = tcp[13];
    if (doff < 20 || doff > len) return;
    size_t plen = len - doff;
    const uint8_t *payload = tcp + doff;

    if (state == ST_SYN_SENT) {
        if ((flags & 0x12) == 0x12) {
            remote_port = sport;
            rcv_nxt = seq + 1;
            snd_nxt = ack;
            got_synack = 1;
            state = ST_EST;
            send_segment(0x10, 0, 0, rcv_nxt);
            serial_write("tcp: ESTABLISHED\n");
        }
        return;
    }

    if (state != ST_EST && state != ST_CLOSE_WAIT)
        return;
    if (sport != remote_port)
        return;

    if (plen > 0 && seq == rcv_nxt) {
        size_t space = sizeof(rx_data) - rx_len;
        size_t copy = plen < space ? plen : space;
        memcpy_l(rx_data + rx_len, payload, copy);
        rx_len += copy;
        rcv_nxt += (uint32_t)plen;
        send_segment(0x10, 0, 0, rcv_nxt);
        serial_write("tcp: RX ");
        serial_write_dec((uint64_t)plen);
        serial_write(" bytes\n");
    } else if (plen > 0) {
        send_segment(0x10, 0, 0, rcv_nxt);
    }

    if (flags & 0x01) {
        if (seq == rcv_nxt)
            rcv_nxt = seq + plen + 1;
        else if (plen == 0)
            rcv_nxt = seq + 1;
        got_fin = 1;
        send_segment(0x10, 0, 0, rcv_nxt);
        state = ST_CLOSE_WAIT;
        serial_write("tcp: FIN\n");
    }
    (void)ack;
}

int tcp_connect(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    if (state != ST_CLOSED)
        tcp_close();

    settle(20);

    net_set_tcp_rx(on_tcp);
    remote_ip[0] = a; remote_ip[1] = b; remote_ip[2] = c; remote_ip[3] = d;
    remote_port = port;
    static uint16_t next_port = 42000;
    local_port = next_port++;
    if (next_port < 42000) next_port = 42000;

    isn_seed = isn_seed * 1103515245u + 12345u;
    snd_nxt = isn_seed;
    rcv_nxt = 0;
    rx_len = 0;
    got_synack = 0;
    got_fin = 0;
    state = ST_SYN_SENT;

    if (send_segment(0x02, 0, 0, 0) != 0) {
        state = ST_CLOSED;
        return -1;
    }
    snd_nxt++;

    for (int i = 0; i < 400; i++) {
        net_poll();
        if (got_synack)
            return 0;
        for (volatile int x = 0; x < 5000; x++) {}
    }
    serial_write("tcp: connect timeout\n");
    state = ST_CLOSED;
    net_set_tcp_rx(0);
    return -1;
}

int tcp_send(const void *data, size_t len) {
    if (state != ST_EST) return -1;
    const uint8_t *p = data;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 800) chunk = 800;
        if (send_segment(0x18, p + off, chunk, rcv_nxt) != 0)
            return -1;
        snd_nxt += (uint32_t)chunk;
        off += chunk;
        settle(5);
    }
    return 0;
}

int tcp_recv(void *buf, size_t maxlen, size_t *out_len, int timeout_iters) {
    for (int i = 0; i < timeout_iters; i++) {
        net_poll();
        if (rx_len > 0) {
            size_t n = rx_len < maxlen ? rx_len : maxlen;
            memcpy_l(buf, rx_data, n);
            for (size_t j = n; j < rx_len; j++)
                rx_data[j - n] = rx_data[j];
            rx_len -= n;
            if (out_len) *out_len = n;
            return 0;
        }
        if (got_fin) {
            if (out_len) *out_len = 0;
            return 0;
        }
        for (volatile int x = 0; x < 5000; x++) {}
    }
    if (out_len) *out_len = 0;
    return -1;
}

void tcp_close(void) {
    if (state == ST_EST || state == ST_CLOSE_WAIT) {
        send_segment(0x11, 0, 0, rcv_nxt); /* FIN+ACK */
        snd_nxt++;
        settle(40);
        /* RST as hard abort if peer still half-open */
        send_segment(0x14, 0, 0, rcv_nxt);
        settle(15);
    }
    state = ST_CLOSED;
    rx_len = 0;
    got_fin = 0;
    got_synack = 0;
    net_set_tcp_rx(0);
    settle(25);
}

int tcp_is_connected(void) {
    return state == ST_EST || state == ST_CLOSE_WAIT;
}
