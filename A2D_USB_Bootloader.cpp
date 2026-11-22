#include "A2D_USB_Bootloader.h"

#if defined(ARDUINO_ARCH_STM32) && defined(A2D_HUB_SAFE_BOOTLOADER_RESET)
#include <Arduino.h>

namespace
{
volatile bool bootloader_requested = false;
const unsigned long disconnect_ms = 500;
}

void A2D_request_usb_bootloader_reset()
{
    bootloader_requested = true;
}

void A2D_service_usb_bootloader_reset()
{
    if (!bootloader_requested) return;
    bootloader_requested = false;

    Serial.end();
    // This board has a fixed 1.5 kohm D+ pull-up and no disconnect transistor.
    // Give the host and intervening hubs a clear disconnect before the MCU
    // restarts with the Maple DFU descriptors and a different USB identity.
    digitalWrite(PA12, LOW);
    pinMode(PA12, OUTPUT_OPEN_DRAIN);
    delay(disconnect_ms);
    NVIC_SystemReset();
}
#endif
