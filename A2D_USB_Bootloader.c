#include "A2D_USB_Bootloader.h"
#include "A2D_HAL.h"
static bool requested;
void A2D_request_usb_bootloader_reset(void) { requested = true; }
void A2D_service_usb_bootloader_reset(void) {
    if (!requested) return;
    requested = false;
    A2D_enter_bootloader();
}
