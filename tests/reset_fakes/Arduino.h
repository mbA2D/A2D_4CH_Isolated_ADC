#ifndef RESET_TEST_ARDUINO_H
#define RESET_TEST_ARDUINO_H

#include <stdint.h>
#include <string>
#include <vector>

#define PA12 12
#define LOW 0
#define OUTPUT_OPEN_DRAIN 3

extern std::vector<std::string> reset_events;
struct ResetSerial
{
    void end() { reset_events.push_back("USB stopped"); }
};
extern ResetSerial Serial;
inline void digitalWrite(int pin, int value)
{
    reset_events.push_back("write " + std::to_string(pin) + " " + std::to_string(value));
}
inline void pinMode(int pin, int mode)
{
    reset_events.push_back("mode " + std::to_string(pin) + " " + std::to_string(mode));
}
inline void delay(unsigned long ms) { reset_events.push_back("wait " + std::to_string(ms)); }
inline void NVIC_SystemReset() { reset_events.push_back("MCU reset"); }

#endif
