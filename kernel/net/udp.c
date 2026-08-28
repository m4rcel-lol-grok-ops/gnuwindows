/*
 * Minimal UDP over existing IPv4 stack
 */

#include <gw/udp.h>
#include <gw/net.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t rx_buf[1500];
static size_t rx_len;
static uint8_t rx_src_ip[4];
static uint16_t rx_src_port;
static uint16_t rx_dst_port;
static int rx_have;

static void memcpy_l(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++;
}

static uint16_t udp_checksum(const uint8_t src[4], const uint8_t dst[4],
                             const uint8_t *udp, size_t udp_len) {
    /* Optional for IPv4; compute for correctness */
    uint32_t sum = 0;
    sum += (src[0] << 8) | src[1];
    sum += (src[2] << 8) | src[3];
    sum += (dst[0] << 8) | dst[1];
    sum += (dst[2] << 8) | dst[3];
    sum += 17;
    sum += (uint16_t)udp_len;
    for (size_t i = 0; i + 1 < udp_len; i += 2)
        sum += (udp[i] << 8) | udp[i + 1];
    if (udp_len & 1) sum += udp[udp_len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void on_udp(const uint8_t *src_ip, const uint8_t *udp, size_t len) {
    if (len < 8) return;
    uint16_t sport = (uint16_t)((udp[0] << 8) | udp[1]);
    uint16_t dport = (uint16_t)((udp[2] << 8) | udp[3]);
    uint16_t ulen = (uint16_t)((udp[4] << 8) | udp[5]);
    if (ulen < 8 || ulen > len) ulen = (uint16_t)len;
    size_t plen = (size_t)ulen - 8;
    if (plen > sizeof(rx_buf)) plen = sizeof(rx_buf);
    memcpy_l(rx_buf, udp + 8, plen);
    memcpy_l(rx_src_ip, src_ip, 4);
    rx_src_port = sport;
    rx_dst_port = dport;
    rx_len = plen;
    rx_have = 1;
}

int udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
             const void *data, size_t len) {
    uint8_t pkt[1480];
    if (len + 8 > sizeof(pkt)) return -1;
    pkt[0] = (uint8_t)(src_port >> 8);
    pkt[1] = (uint8_t)src_port;
    pkt[2] = (uint8_t)(dst_port >> 8);
    pkt[3] = (uint8_t)dst_port;
    uint16_t ulen = (uint16_t)(8 + len);
    pkt[4] = (uint8_t)(ulen >> 8);
    pkt[5] = (uint8_t)ulen;
    pkt[6] = 0; pkt[7] = 0;
    memcpy_l(pkt + 8, data, len);
    /* checksum 0 is allowed for IPv4 UDP */
    (void)udp_checksum;
    return net_ipv4_send(dst_ip, 17, pkt, 8 + len);
}

int udp_recv(uint16_t local_port, void *buf, size_t maxlen, size_t *out_len,
             uint8_t src_ip[4], uint16_t *from_port, int timeout_iters) {
    net_set_udp_rx(on_udp);
    for (int i = 0; i < timeout_iters; i++) {
        net_poll();
        if (rx_have && rx_dst_port == local_port) {
            size_t n = rx_len < maxlen ? rx_len : maxlen;
            memcpy_l(buf, rx_buf, n);
            if (src_ip) memcpy_l(src_ip, rx_src_ip, 4);
            if (from_port) *from_port = rx_src_port;
            if (out_len) *out_len = n;
            rx_have = 0;
            return 0;
        }
        for (volatile int x = 0; x < 4000; x++) {}
    }
    if (out_len) *out_len = 0;
    return -1;
}
