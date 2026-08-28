/*
 * HTTP/1.0 GET — host may be dotted IP or DNS name
 */

#include <gw/tcp.h>
#include <gw/dns.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static int starts(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}

static void pause(int n) {
    for (int i = 0; i < n; i++)
        for (volatile int x = 0; x < 20000; x++) {}
}

static int parse_url(const char *url, char *host, size_t host_max,
                     uint16_t *port, char *path, size_t path_max) {
    if (!starts(url, "http://")) return -1;
    const char *p = url + 7;
    size_t hi = 0;
    while (*p && *p != '/' && *p != ':' && hi + 1 < host_max)
        host[hi++] = *p++;
    host[hi] = 0;
    if (!hi) return -1;
    *port = 80;
    if (*p == ':') {
        p++;
        int pr = 0;
        while (*p >= '0' && *p <= '9') { pr = pr * 10 + (*p - '0'); p++; }
        *port = (uint16_t)pr;
    }
    if (*p == 0) { path[0] = '/'; path[1] = 0; return 0; }
    size_t i = 0;
    while (*p && i + 1 < path_max) path[i++] = *p++;
    path[i] = 0;
    return 0;
}

static int http_get_once(const char *url, void *buf, size_t maxlen, size_t *out_len) {
    char host[128], path[128];
    uint16_t port;
    uint8_t ip[4];
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return -1;
    if (dns_resolve(host, ip) != 0) {
        serial_write("http: resolve failed\n");
        return -1;
    }

    if (tcp_connect(ip[0], ip[1], ip[2], ip[3], port) != 0)
        return -1;

    char req[320];
    char *d = req;
    const char *s = "GET ";
    while (*s) *d++ = *s++;
    s = path;
    while (*s) *d++ = *s++;
    s = " HTTP/1.0\r\nHost: ";
    while (*s) *d++ = *s++;
    s = host;
    while (*s) *d++ = *s++;
    s = "\r\nConnection: close\r\n\r\n";
    while (*s) *d++ = *s++;
    *d = 0;
    size_t req_len = (size_t)(d - req);
    if (tcp_send(req, req_len) != 0) {
        tcp_close();
        return -1;
    }

    size_t total = 0;
    char *out = buf;
    int idle = 0;
    while (total < maxlen && idle < 10) {
        size_t n = 0;
        if (tcp_recv(out + total, maxlen - total, &n, 120) != 0) {
            idle++;
            continue;
        }
        if (n == 0) break;
        total += n;
        idle = 0;
    }
    tcp_close();

    size_t body = 0;
    for (size_t i = 0; i + 3 < total; i++) {
        if (out[i]=='\r'&&out[i+1]=='\n'&&out[i+2]=='\r'&&out[i+3]=='\n') {
            body = i + 4; break;
        }
    }
    if (body == 0) {
        for (size_t i = 0; i + 1 < total; i++) {
            if (out[i]=='\n'&&out[i+1]=='\n') { body = i + 2; break; }
        }
    }
    if (body == 0 || body >= total) {
        if (out_len) *out_len = total;
        serial_write("http: raw ");
        serial_write_dec(total);
        serial_write(" bytes\n");
        return total > 0 ? 0 : -1;
    }
    size_t blen = total - body;
    for (size_t i = 0; i < blen; i++) out[i] = out[body + i];
    if (out_len) *out_len = blen;
    serial_write("http: body ");
    serial_write_dec(blen);
    serial_write(" bytes\n");
    return 0;
}

int http_get(const char *url, void *buf, size_t maxlen, size_t *out_len) {
    serial_write("http: GET ");
    serial_write(url);
    serial_write("\n");
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) {
            serial_write("http: retry\n");
            pause(80 * attempt);
        }
        size_t n = 0;
        if (http_get_once(url, buf, maxlen, &n) == 0 && n > 0) {
            if (out_len) *out_len = n;
            return 0;
        }
        pause(40);
    }
    return -1;
}
