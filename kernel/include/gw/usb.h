#ifndef GW_USB_H
#define GW_USB_H

void usb_init(void);
/* Poll interrupt transfers; call from shell/timer path */
void usb_poll(void);

#endif
