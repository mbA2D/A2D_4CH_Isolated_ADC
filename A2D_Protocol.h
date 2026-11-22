#ifndef A2D_PROTOCOL_H
#define A2D_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

namespace a2d {
constexpr size_t LineCapacity = 200;
enum class Source { Usb, Rs485 };
enum class Command {
    Identity, Reset, Clear, Trigger, Led, Voltage, RawVoltage,
    CalibrationReset, CalibrationSave, Calibrate, Calibration,
    Gain, DataRate, Mode, AddressSet, Address, AddressSave, Bootloader
};
// Values own their storage. No pointers into a parser's temporary buffer escape.
struct Request {
    Command command;
    Source source;
    uint8_t channel; // Original chain channel; 0 retains the legacy group semantics.
    uint8_t value;
    float points[4]; // ACTUAL1, MEASURED1, ACTUAL2, MEASURED2
    char original[LineCapacity];
};
enum class ParseResult { Ready, Ignore, InvalidCommand, InvalidArguments };
ParseResult parse(const char *line, Source source, uint8_t address, Request &request);
bool channelOnDevice(uint8_t channel, uint8_t address);
uint8_t localChannel(uint8_t channel, uint8_t address);
uint8_t channelAddress(uint8_t channel); // Requires channel 1..128.
}
#endif
