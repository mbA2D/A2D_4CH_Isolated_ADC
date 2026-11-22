#include "A2D_Protocol.h"
#include "A2D_Calibration.h"
#include <A2D_Scpi.h>
#include <string.h>

namespace a2d {
namespace {
bool number(const char *text, uint8_t maximum, uint8_t &value) {
    uint32_t parsed;
    if (!a2d_scpi::unsignedInteger(text, maximum, parsed)) return false;
    value = static_cast<uint8_t>(parsed);
    return true;
}
constexpr a2d_scpi::CommandSpec specification(const char *name, Command command,
                                             uint8_t arguments, bool trailingQuery = false) {
    return {name, static_cast<uint16_t>(command), arguments, trailingQuery};
}
const a2d_scpi::CommandSpec specifications[] = {
    specification("*IDN?", Command::Identity, 0), specification("*RST", Command::Reset, 0),
    specification("*CLS", Command::Clear, 0), specification("*TRG", Command::Trigger, 0),
    specification("INSTR:LED", Command::Led, 1), specification("MEAS:VOLT", Command::Voltage, 1, true),
    specification("MEAS:VOLT:ADC", Command::RawVoltage, 1, true), specification("CAL:RESET", Command::CalibrationReset, 1),
    specification("CAL:SAVE", Command::CalibrationSave, 1), specification("CAL:VOLT", Command::Calibrate, 5),
    specification("CAL", Command::Calibration, 1, true), specification("INSTR:GAIN", Command::Gain, 2),
    specification("INSTR:DR", Command::DataRate, 2), specification("INSTR:MODE", Command::Mode, 2),
    specification("INSTR:RS485:SET", Command::AddressSet, 1), specification("INSTR:RS485?", Command::Address, 0),
    specification("INSTR:RS485:SAVE", Command::AddressSave, 0),
    specification("SYST:BOOT", Command::Bootloader, 0)
};
}
bool channelOnDevice(uint8_t channel, uint8_t address) {
    return channel >= address * 4 + 1 && channel <= address * 4 + 4;
}
uint8_t localChannel(uint8_t channel, uint8_t address) {
    return channelOnDevice(channel, address) ? static_cast<uint8_t>((channel - 1) % 4 + 1) : channel;
}
uint8_t channelAddress(uint8_t channel) { return static_cast<uint8_t>((channel - 1) / 4); }

ParseResult parse(const char *line, Source source, uint8_t address, Request &request) {
    if (strlen(line) >= LineCapacity) return ParseResult::InvalidCommand;
    char buffer[LineCapacity];
    strcpy(buffer, line);
    const char *header = line + strspn(line, " \t\r");
    if (strcspn(header, " \t\r") >= 32) return ParseResult::InvalidCommand;
    a2d_scpi::ParsedCommand parsed;
    switch (a2d_scpi::parse(buffer, specifications, sizeof(specifications) / sizeof(specifications[0]), parsed)) {
    case a2d_scpi::ParseResult::Empty: return ParseResult::Ignore;
    case a2d_scpi::ParseResult::UnknownCommand: // Preserve the ADC's unknown-command response.
    case a2d_scpi::ParseResult::InvalidArguments: return ParseResult::InvalidArguments;
    case a2d_scpi::ParseResult::Ready: break;
    }
    const char *const *arguments = parsed.arguments;
    Request candidate = {};
    candidate.command = static_cast<Command>(parsed.spec->id);
    candidate.source = source;
    if (candidate.command == Command::Bootloader && source != Source::Usb)
        return ParseResult::InvalidArguments;
    int8_t channelIndex = -1;
    switch (candidate.command) {
    case Command::Voltage: case Command::RawVoltage: case Command::CalibrationReset:
    case Command::CalibrationSave: case Command::Calibrate: case Command::Calibration:
        channelIndex = 0; break;
    case Command::Gain: case Command::DataRate: case Command::Mode:
        channelIndex = 1; break;
    default: break;
    }
    if (channelIndex >= 0) {
        if (!number(arguments[channelIndex], 128, candidate.channel)) return ParseResult::InvalidArguments;
        if (source == Source::Rs485 && candidate.channel && !channelOnDevice(candidate.channel, address))
            return ParseResult::InvalidArguments;
    }
    switch (candidate.command) {
    case Command::Led:
        if (!number(arguments[0], 1, candidate.value)) return ParseResult::InvalidArguments;
        break;
    case Command::AddressSet:
        if (!number(arguments[0], 31, candidate.value)) return ParseResult::InvalidArguments;
        break;
    case Command::Gain: case Command::DataRate: case Command::Mode:
        if (!number(arguments[0], 240, candidate.value)) return ParseResult::InvalidArguments;
        if (candidate.command == Command::Gain && candidate.value != 1 && candidate.value != 2 && candidate.value != 4 && candidate.value != 8)
            return ParseResult::InvalidArguments;
        if (candidate.command == Command::DataRate && candidate.value != 15 && candidate.value != 60 && candidate.value != 240)
            return ParseResult::InvalidArguments;
        if (candidate.command == Command::Mode && candidate.value > 1) return ParseResult::InvalidArguments;
        break;
    case Command::Calibrate: {
        if (!candidate.channel) return ParseResult::InvalidArguments;
        for (uint8_t i = 0; i < 4; ++i)
            if (!a2d_scpi::realNumber(arguments[i + 1], candidate.points[i])) return ParseResult::InvalidArguments;
        Coefficients coefficients;
        if (!Calibration::calculate(candidate.points[0], candidate.points[1], candidate.points[2], candidate.points[3], coefficients))
            return ParseResult::InvalidArguments;
        break;
    }
    default: break;
    }
    strcpy(candidate.original, line);
    request = candidate; // Publish only a fully validated, independent request.
    return ParseResult::Ready;
}
}
