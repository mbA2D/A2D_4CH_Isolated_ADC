#include <A2D_Application.h>
#include <A2D_USB_Bootloader.h>
#include <A2D_USB_Session.h>
#ifndef A2D_DEVICE_SERIAL
#define A2D_DEVICE_SERIAL "0002"
#endif
// Composition and platform startup only.
A2D_4CH_Isolated_ADC g_a2d_adc;
HardwareSerial Serial3(PB11, PB10);
a2d::Transport g_transport(Serial, Serial3, g_a2d_adc);
a2d::Dispatcher g_dispatcher(g_a2d_adc, g_transport, A2D_DEVICE_SERIAL);
a2d::Application g_application(g_a2d_adc, g_transport, g_dispatcher);
void setup() {
    Wire.begin();
    A2D_install_usb_session_hooks();
    Serial.begin(115200);
    Serial3.begin(57600);
    g_a2d_adc.init();
#ifdef FORCE_EEPROM_REINIT
    g_a2d_adc.force_eeprom_reinit();
#endif
}
void loop() {
    A2D_service_usb_bootloader_reset();
    g_application.poll();
}
