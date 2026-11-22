#ifndef A2D_USB_BOOTLOADER_H
#define A2D_USB_BOOTLOADER_H

// Requested only after a complete USB command has passed validation.
void A2D_request_usb_bootloader_reset();
#if defined(ARDUINO_ARCH_STM32) && defined(A2D_HUB_SAFE_BOOTLOADER_RESET)
// Called from loop(), outside USB interrupts.
void A2D_service_usb_bootloader_reset();
#else
inline void A2D_service_usb_bootloader_reset() {}
#endif

#endif
