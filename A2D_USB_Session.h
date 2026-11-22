#ifndef A2D_USB_SESSION_H
#define A2D_USB_SESSION_H
#include <stdint.h>

// STM32 USB adapter. Session events are captured in the CDC callbacks even
// when a complete close/reopen happens between application polls.
void A2D_install_usb_session_hooks();
uint32_t A2D_usb_session_generation();
bool A2D_usb_session_open();

#endif
