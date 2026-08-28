#ifndef GW_UDP_H
#define GW_UDP_H

#include <stdint.h>
#include <stddef.h>

int udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
             const void *data, size_t len);
/* Poll until a UDP datagram arrives for src_port; returns payload length or -1 */
int udp_recv(uint16_t src_port, void *buf, size_t maxlen, size_t *out_len,
             uint8_t src_ip[4], uint16_t *from_port, int timeout_iters);

#endif
