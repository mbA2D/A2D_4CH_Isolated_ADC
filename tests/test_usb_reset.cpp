#define ARDUINO_ARCH_STM32
#define A2D_HUB_SAFE_BOOTLOADER_RESET

#include <iostream>
#include <stdexcept>
#include "reset_fakes/Arduino.h"

std::vector<std::string> reset_events;
ResetSerial Serial;
#include "../A2D_USB_Bootloader.cpp"

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)

int main()
{
    try
    {
        A2D_service_usb_bootloader_reset();
        CHECK(reset_events.empty());
        std::cout << "PASS idle reset service leaves USB connected\n";

        A2D_request_usb_bootloader_reset();
        A2D_request_usb_bootloader_reset();
        CHECK(reset_events.empty());
        std::cout << "PASS validated command requests deferred hardware work\n";

        A2D_service_usb_bootloader_reset();
        const std::vector<std::string> expected = {
            "USB stopped", "write 12 0", "mode 12 3", "wait 500", "MCU reset"
        };
        CHECK(reset_events == expected);
        std::cout << "PASS main loop disconnects D+ before resetting\n";
        A2D_service_usb_bootloader_reset();
        CHECK(reset_events == expected);
        std::cout << "PASS reset request is handled once\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "4 USB reset checks passed\n";
}
