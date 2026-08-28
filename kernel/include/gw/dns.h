#ifndef GW_DNS_H
#define GW_DNS_H

#include <stdint.h>

/* Resolve hostname to IPv4 via DNS at 10.0.2.3 (QEMU user-net).
 * If name is already A.B.C.D, parses it. Returns 0 on success. */
int dns_resolve(const char *name, uint8_t out_ip[4]);

#endif
