#ifndef GW_NE2K_H
#define GW_NE2K_H

#include <stdint.h>
#include <stddef.h>

int  ne2k_init(void);
int  ne2k_send(const void *frame, size_t len);
/* Poll for a received frame; returns length or 0 if none, -1 on error */
int  ne2k_poll(void *buf, size_t maxlen);
const uint8_t *ne2k_mac(void);

#endif
