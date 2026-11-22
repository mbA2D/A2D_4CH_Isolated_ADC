#include "A2D_Dispatcher.h"
#include "A2D_USB_Bootloader.h"
#include <math.h>
namespace a2d {
void Dispatcher::read(const Request &request, uint8_t channel) {
    if (channel == 0 && request.source == Source::Rs485) return;
    const uint8_t first = channel ? channel - 1 : 0;
    const uint8_t end = channel ? channel : 4;
    float values[8]; uint8_t count = 0;
    for (uint8_t i = first; i < end; ++i) {
        if (request.command == Command::Calibration) {
            values[count++] = board_.get_cal_offset(i);
            values[count++] = board_.get_cal_gain(i);
        } else values[count++] = request.command == Command::Voltage ? board_.measure_voltage(i) : board_.measure_raw_voltage(i);
    }
    for (uint8_t i = 0; i < count; ++i) if (!isfinite(values[i])) {
        transport_.reply(request.source, "ERROR:ADC READ");
        return; // Never emit a partial group or a plausible number for a failed channel.
    }
    // Arduino Print emits the nonnumeric token "ovf" beyond this bound, even
    // for finite values. Check the complete group before emitting any numbers.
    for (uint8_t i = 0; i < count; ++i) if (fabsf(values[i]) > 4294967040.0f) {
        transport_.reply(request.source, "ERROR:NUMERIC RANGE");
        return;
    }
    Print &out = transport_.beginReply(request.source);
    for (uint8_t i = 0; i < count; ++i) {
        if (i) out.print(",");
        out.print(values[i], request.command == Command::Calibration ? 5 : 4);
    }
    out.println();
    transport_.endReply(request.source);
}
void Dispatcher::execute(const Request &request) {
    const Command cmd = request.command;
    const uint8_t channel = localChannel(request.channel, board_.get_rs485_addr());
    if (channel > 4) {
        if (request.source == Source::Usb && board_.get_rs485_addr() == 0)
            transport_.forward(channelAddress(request.channel), request.original,
                cmd == Command::Voltage || cmd == Command::RawVoltage || cmd == Command::Calibration);
        return;
    }
    switch (cmd) {
    case Command::Bootloader:
        if (request.source == Source::Usb) A2D_request_usb_bootloader_reset();
        break;
    case Command::Identity: {
        Print &out = transport_.beginReply(request.source);
        out.print(MANUFACTURER); out.print(","); out.print(MODEL); out.print(",");
        out.print(serial_); out.print(","); out.println(VERSION);
        transport_.endReply(request.source);
        break;
    }
    case Command::Reset:
        if (!board_.reset()) transport_.reply(request.source, "ERROR:RESET FAILED");
        break;
    case Command::Clear: break;
    case Command::Trigger:
        if (!board_.trigger_all_single_shot()) transport_.reply(request.source, "ERROR:ADC CONFIG");
        break;
    case Command::Led: board_.set_led(request.value != 0); break;
    case Command::AddressSet: board_.set_rs485_addr(request.value); break;
    case Command::AddressSave:
        if (!board_.save_rs485_addr()) transport_.reply(request.source, "ERROR:STORAGE WRITE");
        break;
    case Command::Address:
        transport_.beginReply(request.source).println(board_.get_rs485_addr());
        transport_.endReply(request.source);
        break;
    case Command::Voltage: case Command::RawVoltage: case Command::Calibration:
        read(request, channel); break;
    case Command::Calibrate:
        if (!board_.try_calibrate_voltage(channel - 1, request.points[0], request.points[1], request.points[2], request.points[3]))
            transport_.reply(request.source, "ERROR:INVALID ARGUMENTS");
        break;
    case Command::CalibrationReset: case Command::CalibrationSave:
        if (channel == 0 && request.source == Source::Rs485) break;
        if (cmd == Command::CalibrationReset) {
            if (!channel) board_.reset_all_calibration(); else board_.reset_calibration(channel - 1);
        } else {
            const bool ok = !channel ? board_.save_all_calibration() : board_.save_calibration(channel - 1);
            if (!ok) transport_.reply(request.source, "ERROR:STORAGE WRITE");
        }
        break;
    case Command::Gain: case Command::DataRate: case Command::Mode: {
        uint8_t value = request.value;
        if (cmd == Command::Gain) value = value == 1 ? MCP3425_GAIN_1 : value == 2 ? MCP3425_GAIN_2 : value == 4 ? MCP3425_GAIN_4 : MCP3425_GAIN_8;
        if (cmd == Command::DataRate) value = value == 15 ? MCP3425_DR_15SPS : value == 60 ? MCP3425_DR_60SPS : MCP3425_DR_240SPS;
        const uint8_t first = channel ? channel - 1 : 0;
        const uint8_t end = channel ? channel : 4;
        bool ok = true;
        for (uint8_t i = first; i < end; ++i) {
            const bool configured = cmd == Command::Gain ? board_.set_gain(i, value) :
                cmd == Command::DataRate ? board_.set_data_rate(i, value) : board_.set_mode(i, value);
            if (!configured) ok = false;
        }
        if (!ok) transport_.reply(request.source, "ERROR:ADC CONFIG");
        break;
    }
    }
}
}
