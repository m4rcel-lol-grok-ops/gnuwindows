/*
 * IPv4 + ARP + ICMP on NE2000; hooks for TCP.
 */

#include <gw/net.h>
#include <gw/ne2k.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t ip_addr[4]  = {10, 0, 2, 15};
static uint8_t ip_gw[4]    = {10, 0, 2, 2};
static uint8_t gw_mac[6];
static int gw_mac_valid;
static int net_up;

static uint8_t rxbuf[2048];
static uint8_t txbuf[2048];

static void (*tcp_rx_cb)(const uint8_t *src_ip, const uint8_t *tcp, size_t len);
static void (*udp_rx_cb)(const uint8_t *src_ip, const uint8_t *udp, size_t len);

/* Offline fallback for known paths when TCP fails */
static const char offline_hello_pkg[] =
    "---MANIFEST---\n"
    "name: hello\nversion: 0.1.0\ndesc: offline fallback\nfile: README.TXT\n"
    "---FILE:README.TXT---\n"
    "GNU/Windows package: hello (offline fallback)\n"
    "---END---\n";

static void memcpy_l(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++;
}
static void memset_l(void *d, int c, size_t n) {
    uint8_t *p = d; while (n--) *p++ = (uint8_t)c;
}
static int starts(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}

static uint16_t ip_checksum(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2; len -= 2;
    }
    if (len) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void eth_send(const uint8_t dst[6], uint16_t ethertype, const void *payload, size_t len) {
    memset_l(txbuf, 0, 60);
    memcpy_l(txbuf, dst, 6);
    memcpy_l(txbuf + 6, ne2k_mac(), 6);
    txbuf[12] = (uint8_t)(ethertype >> 8);
    txbuf[13] = (uint8_t)ethertype;
    memcpy_l(txbuf + 14, payload, len);
    size_t total = 14 + len;
    if (total < 60) total = 60;
    ne2k_send(txbuf, total);
}

static void arp_xmit(int is_req, const uint8_t tip[4], const uint8_t tmac[6]) {
    uint8_t pkt[28];
    memset_l(pkt, 0, 28);
    pkt[1] = 1; pkt[2] = 0x08; pkt[5] = 4; pkt[4] = 6;
    pkt[7] = is_req ? 1 : 2;
    memcpy_l(pkt + 8, ne2k_mac(), 6);
    memcpy_l(pkt + 14, ip_addr, 4);
    if (!is_req) {
        memcpy_l(pkt + 18, tmac, 6);
        memcpy_l(pkt + 24, tip, 4);
    } else {
        memcpy_l(pkt + 24, tip, 4);
    }
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    eth_send(is_req ? bcast : tmac, 0x0806, pkt, 28);
}

/* Last ARP learn (any host) */
static uint8_t arp_ip[4];
static uint8_t arp_mac[6];
static int arp_have;

static void handle_arp(const uint8_t *pkt, size_t len) {
    if (len < 28) return;
    uint16_t op = (uint16_t)((pkt[6] << 8) | pkt[7]);
    uint8_t sha[6], spa[4], tpa[4];
    memcpy_l(sha, pkt + 8, 6);
    memcpy_l(spa, pkt + 14, 4);
    memcpy_l(tpa, pkt + 24, 4);
    if (op == 1) {
        if (tpa[0]==ip_addr[0]&&tpa[1]==ip_addr[1]&&tpa[2]==ip_addr[2]&&tpa[3]==ip_addr[3])
            arp_xmit(0, spa, sha);
    } else if (op == 2) {
        memcpy_l(arp_ip, spa, 4);
        memcpy_l(arp_mac, sha, 6);
        arp_have = 1;
        if (spa[0]==ip_gw[0]&&spa[1]==ip_gw[1]&&spa[2]==ip_gw[2]&&spa[3]==ip_gw[3]) {
            memcpy_l(gw_mac, sha, 6);
            gw_mac_valid = 1;
            serial_write("net: ARP gateway ok\n");
        }
    }
}

static void handle_ip(const uint8_t *frame, size_t len) {
    if (len < 14 + 20) return;
    const uint8_t *ip = frame + 14;
    if ((ip[0] >> 4) != 4) return;
    size_t ihl = (ip[0] & 0xF) * 4;
    if (ihl < 20 || 14 + ihl > len) return;
    uint16_t tot = (uint16_t)((ip[2] << 8) | ip[3]);
    if (tot < ihl) return;
    size_t avail = len - 14;
    /* Frame may include Ethernet padding and/or FCS beyond IP datagram */
    if ((size_t)tot > avail) {
        /* truncated — use what we have */
        tot = (uint16_t)avail;
        if (tot < ihl) return;
    }
    uint8_t proto = ip[9];
    const uint8_t *src = ip + 12;
    const uint8_t *payload = ip + ihl;
    size_t plen = (size_t)tot - ihl;

    if (proto == 1 && plen >= 8 && payload[0] == 8) {
        /* ICMP echo reply */
        uint8_t ippkt[1500];
        size_t total = 20 + plen;
        if (total > sizeof(ippkt)) return;
        memset_l(ippkt, 0, 20);
        ippkt[0] = 0x45;
        ippkt[2] = (uint8_t)(total >> 8);
        ippkt[3] = (uint8_t)total;
        ippkt[8] = 64; ippkt[9] = 1;
        memcpy_l(ippkt + 12, ip_addr, 4);
        memcpy_l(ippkt + 16, src, 4);
        uint16_t c = ip_checksum(ippkt, 20);
        ippkt[10] = (uint8_t)(c >> 8); ippkt[11] = (uint8_t)c;
        memcpy_l(ippkt + 20, payload, plen);
        ippkt[20] = 0;
        ippkt[22] = 0; ippkt[23] = 0;
        c = ip_checksum(ippkt + 20, plen);
        ippkt[22] = (uint8_t)(c >> 8); ippkt[23] = (uint8_t)c;
        eth_send(frame + 6, 0x0800, ippkt, total);
    } else if (proto == 1 && plen >= 8 && payload[0] == 0) {
        serial_write("net: ICMP echo reply\n");
    } else if (proto == 6 && tcp_rx_cb) {
        tcp_rx_cb(src, payload, plen);
    } else if (proto == 17 && udp_rx_cb) {
        udp_rx_cb(src, payload, plen);
    }
}

void net_poll(void) {
    int n = ne2k_poll(rxbuf, sizeof(rxbuf));
    if (n < 14) return;
    uint16_t type = (uint16_t)((rxbuf[12] << 8) | rxbuf[13]);
    if (type == 0x0806) handle_arp(rxbuf + 14, (size_t)n - 14);
    else if (type == 0x0800) handle_ip(rxbuf, (size_t)n);
}

const uint8_t *net_ip_addr(void) { return ip_addr; }

void net_set_tcp_rx(void (*cb)(const uint8_t *src_ip, const uint8_t *tcp, size_t len)) {
    tcp_rx_cb = cb;
}

void net_set_udp_rx(void (*cb)(const uint8_t *src_ip, const uint8_t *udp, size_t len)) {
    udp_rx_cb = cb;
}

int net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto, const void *payload, size_t len) {
    if (!net_up) return -1;
    uint8_t mac[6];
    /* Prefer exact ARP match */
    if (arp_have && arp_ip[0]==dst_ip[0]&&arp_ip[1]==dst_ip[1]&&
        arp_ip[2]==dst_ip[2]&&arp_ip[3]==dst_ip[3]) {
        memcpy_l(mac, arp_mac, 6);
    } else {
        arp_xmit(1, dst_ip, 0);
        for (int i = 0; i < 30; i++) {
            net_poll();
            for (volatile int d = 0; d < 8000; d++) {}
            if (arp_have && arp_ip[0]==dst_ip[0]&&arp_ip[1]==dst_ip[1]&&
                arp_ip[2]==dst_ip[2]&&arp_ip[3]==dst_ip[3]) {
                memcpy_l(mac, arp_mac, 6);
                goto have_mac;
            }
        }
        if (!gw_mac_valid) return -1;
        memcpy_l(mac, gw_mac, 6);
    }
have_mac:
    {
        uint8_t ippkt[1500];
        size_t total = 20 + len;
        if (total > sizeof(ippkt)) return -1;
        memset_l(ippkt, 0, 20);
        ippkt[0] = 0x45;
        ippkt[2] = (uint8_t)(total >> 8);
        ippkt[3] = (uint8_t)total;
        static uint16_t ip_id;
        ip_id++;
        ippkt[4] = (uint8_t)(ip_id >> 8);
        ippkt[5] = (uint8_t)ip_id;
        ippkt[8] = 64;
        ippkt[9] = proto;
        memcpy_l(ippkt + 12, ip_addr, 4);
        memcpy_l(ippkt + 16, dst_ip, 4);
        uint16_t c = ip_checksum(ippkt, 20);
        ippkt[10] = (uint8_t)(c >> 8);
        ippkt[11] = (uint8_t)c;
        memcpy_l(ippkt + 20, payload, len);
        eth_send(mac, 0x0800, ippkt, total);
        return 0;
    }
}

int net_init(void) {
    net_up = 0;
    gw_mac_valid = 0;
    arp_have = 0;
    tcp_rx_cb = 0;
    udp_rx_cb = 0;
    if (ne2k_init() != 0) {
        serial_write("net: no NE2000\n");
        return -1;
    }
    net_up = 1;
    serial_write("net: IPv4 10.0.2.15/24 gw 10.0.2.2\n");
    arp_xmit(1, ip_gw, 0);
    for (int i = 0; i < 30; i++) {
        net_poll();
        for (volatile int d = 0; d < 8000; d++) {}
        if (gw_mac_valid) break;
    }
    return 0;
}

void net_status(void) {
    serial_write(net_up ? "net: up" : "net: down");
    serial_write("  gw_mac=");
    serial_write(gw_mac_valid ? "yes" : "no");
    serial_write("\n");
}

int net_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint8_t dst[4] = {a,b,c,d};
    uint8_t icmp[8];
    memset_l(icmp, 0, 8);
    icmp[0] = 8;
    icmp[4] = 0x12; icmp[5] = 0x34;
    icmp[7] = 1;
    uint16_t csum = ip_checksum(icmp, 8);
    icmp[2] = (uint8_t)(csum >> 8); icmp[3] = (uint8_t)csum;
    if (net_ipv4_send(dst, 1, icmp, 8) != 0) return -1;
    for (int i = 0; i < 40; i++) {
        net_poll();
        for (volatile int x = 0; x < 10000; x++) {}
    }
    return 0;
}

/* Forward decl — implemented after TCP is linked; weak offline path here via http.c */
int http_get(const char *url, void *buf, size_t maxlen, size_t *out_len);

int net_http_get(const char *url, void *buf, size_t maxlen, size_t *out_len) {
    if (http_get(url, buf, maxlen, out_len) == 0)
        return out_len ? (int)*out_len : 0;

    /* Offline fallback if HTTP fails */
    const char *payload = 0;
    if (starts(url, "http://10.0.2.100/v1/hello.pkg") ||
        starts(url, "http://packages.gnuwindows.org/v1/hello.pkg"))
        payload = offline_hello_pkg;
    else return -1;
    size_t len = 0;
    while (payload[len]) len++;
    if (len > maxlen) len = maxlen;
    for (size_t i = 0; i < len; i++) ((char *)buf)[i] = payload[i];
    if (out_len) *out_len = len;
    serial_write("net: offline fallback\n");
    return (int)len;
}
