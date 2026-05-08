#pragma once

#include <stdbool.h>
#include <stddef.h>

void usb_serial_init(void);
bool usb_serial_connected(void);
void usb_serial_send(const char *msg);        /* msg must include \n */
bool usb_serial_readline(char *buf, size_t maxlen); /* returns true if a complete line was read */
