#ifndef GW_SERIAL_H
#define GW_SERIAL_H

#include <stdint.h>
#include <stddef.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint64_t val);

/* Input */
int  serial_rx_ready(void);
int  serial_getc_nonblock(void);  /* -1 if none */
char serial_getc(void);           /* blocks with pause/yield-friendly spin */

#endif
