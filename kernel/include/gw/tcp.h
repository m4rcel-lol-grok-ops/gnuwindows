#ifndef GW_TCP_H
#define GW_TCP_H

#include <stdint.h>
#include <stddef.h>

/* Blocking TCP client over existing IPv4/NE2000 stack */
int tcp_connect(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);
int tcp_send(const void *data, size_t len);
int tcp_recv(void *buf, size_t maxlen, size_t *out_len, int timeout_iters);
void tcp_close(void);
int tcp_is_connected(void);

#endif
