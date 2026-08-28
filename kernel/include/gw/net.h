#ifndef GW_NET_H
#define GW_NET_H

#include <stddef.h>
#include <stdint.h>

int  net_init(void);
int  net_http_get(const char *url, void *buf, size_t maxlen, size_t *out_len);
void net_status(void);
int  net_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

void net_poll(void);
int  net_ipv4_send(const uint8_t dst_ip[4], uint8_t proto,
                   const void *payload, size_t len);
const uint8_t *net_ip_addr(void);
void net_set_tcp_rx(void (*cb)(const uint8_t *src_ip, const uint8_t *tcp, size_t len));
void net_set_udp_rx(void (*cb)(const uint8_t *src_ip, const uint8_t *udp, size_t len));

#endif
