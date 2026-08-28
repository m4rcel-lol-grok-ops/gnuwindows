#ifndef GW_KBD_H
#define GW_KBD_H

void kbd_init(void);
void kbd_irq(void);
/* -1 if empty; else ASCII (or 8 for backspace, 3 for Ctrl+C) */
int  kbd_getc_nonblock(void);

#endif
