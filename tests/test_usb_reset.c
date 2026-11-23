#include <stdio.h>
#include "A2D_HAL.h"
#include "A2D_USB_Bootloader.h"
static unsigned resets;
void A2D_enter_bootloader(void) { ++resets; }
#define CHECK(c) do {if(!(c)) {fprintf(stderr,"FAIL %s\n",#c);return 1;}} while(0)
int main(void) {
    A2D_service_usb_bootloader_reset(); CHECK(resets==0);
    A2D_request_usb_bootloader_reset(); A2D_request_usb_bootloader_reset(); CHECK(resets==0);
    A2D_service_usb_bootloader_reset(); CHECK(resets==1);
    A2D_service_usb_bootloader_reset(); CHECK(resets==1);
    puts("4 C deferred bootloader checks passed");return 0;
}
