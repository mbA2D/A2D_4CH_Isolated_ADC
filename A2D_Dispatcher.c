#include "A2D_Dispatcher.h"
#include "A2D_USB_Bootloader.h"
#include <math.h>
static void read_values(A2D_Transport *transport, const A2D_Request *request, uint8_t channel) {
    if (channel == 0 && request->source == A2D_SOURCE_RS485) return;
    A2D_Board *board = transport->board;
    const uint8_t first = channel ? channel - 1 : 0, end = channel ? channel : 4;
    float values[8]; uint8_t count = 0;
    for (uint8_t i = first; i < end; ++i) {
        if (request->command == A2D_CMD_CALIBRATION) {
            values[count++] = a2d_board_cal_offset(board, i); values[count++] = a2d_board_cal_gain(board, i);
        } else values[count++] = request->command == A2D_CMD_VOLTAGE ?
            a2d_board_measure_voltage(board, i) : a2d_board_measure_raw_voltage(board, i);
    }
    for (uint8_t i = 0; i < count; ++i) if (!isfinite(values[i])) {
        a2d_transport_error(transport, A2D_ERROR_ADC_READ); return;
    }
    /* Validate the entire group before starting a numeric reply. */
    for (uint8_t i = 0; i < count; ++i) if (fabsf(values[i]) > 4294967040.0f) {
        a2d_transport_error(transport, A2D_ERROR_RANGE); return;
    }
    A2D_Writer out = a2d_transport_begin_reply(transport, request->source);
    for (uint8_t i = 0; i < count; ++i) {
        if (i) a2d_writer_text(&out, ",");
        a2d_writer_float(&out, values[i], request->command == A2D_CMD_CALIBRATION ? 5 : 4);
    }
    a2d_writer_newline(&out); a2d_transport_end_reply(transport, request->source);
}
static void read_configuration(A2D_Transport *transport, const A2D_Request *request, uint8_t channel) {
    if (channel == 0 && request->source == A2D_SOURCE_RS485) return;
    const uint8_t first = channel ? channel - 1 : 0, end = channel ? channel : 4;
    uint8_t values[4], count = 0;
    for (uint8_t i = first; i < end; ++i) {
        uint8_t gain, rate, mode;
        if (!a2d_board_get_configuration(transport->board, i, &gain, &rate, &mode)) {
            a2d_transport_error(transport, A2D_ERROR_ADC_CONFIG); return;
        }
        values[count++] = request->command == A2D_CMD_GAIN_QUERY ? gain :
            request->command == A2D_CMD_DATA_RATE_QUERY ? rate : mode;
    }
    A2D_Writer out = a2d_transport_begin_reply(transport, request->source);
    for (uint8_t i = 0; i < count; ++i) {
        if (i) a2d_writer_text(&out, ",");
        a2d_writer_uint(&out, values[i]);
    }
    a2d_writer_newline(&out); a2d_transport_end_reply(transport, request->source);
}
void a2d_dispatch_execute(A2D_Transport *transport, const A2D_Request *request) {
    A2D_Board *board = transport->board;
    const A2D_Command command = request->command;
    const uint8_t channel = a2d_local_channel(request->channel, a2d_board_address(board));
    if (channel > 4) {
        if (request->source == A2D_SOURCE_USB && a2d_board_address(board) == 0)
            a2d_transport_forward(transport, a2d_channel_address(request->channel), request->original,
                command == A2D_CMD_VOLTAGE || command == A2D_CMD_RAW_VOLTAGE || command == A2D_CMD_CALIBRATION ||
                command == A2D_CMD_GAIN_QUERY || command == A2D_CMD_DATA_RATE_QUERY || command == A2D_CMD_MODE_QUERY);
        else a2d_transport_error(transport, A2D_ERROR_RANGE);
        return;
    }
    switch (command) {
    case A2D_CMD_SERIAL_SET:
        if (request->source != A2D_SOURCE_USB) break;
        if (a2d_board_serial_number(board) != UINT32_MAX && a2d_board_serial_number(board) != request->serial)
            a2d_transport_error(transport, A2D_ERROR_IDENTITY_LOCKED);
        else if (!a2d_board_provision_serial(board, request->serial)) a2d_transport_error(transport, A2D_ERROR_STORAGE);
        break;
    case A2D_CMD_BOOTLOADER:
        if (request->source == A2D_SOURCE_USB) A2D_request_usb_bootloader_reset();
        break;
    case A2D_CMD_IDENTITY: {
        A2D_Writer out = a2d_transport_begin_reply(transport, request->source);
        a2d_writer_text(&out, MANUFACTURER); a2d_writer_text(&out, ",");
        a2d_writer_text(&out, MODEL); a2d_writer_text(&out, ",");
        a2d_writer_text(&out, a2d_board_serial(board)); a2d_writer_text(&out, ",");
        a2d_writer_text(&out, VERSION); a2d_writer_newline(&out);
        a2d_transport_end_reply(transport, request->source); break;
    }
    case A2D_CMD_RESET:
        if (!a2d_board_reset(board)) a2d_transport_error(transport, A2D_ERROR_RESET);
        break;
    case A2D_CMD_CLEAR: a2d_scpi_errors_clear(&transport->errors); break;
    case A2D_CMD_ERROR: {
        const int16_t code = a2d_scpi_errors_peek(&transport->errors);
        A2D_Writer out = a2d_transport_begin_reply(transport, request->source);
        if (code < 0) a2d_writer_text(&out, "-");
        a2d_writer_uint(&out, code < 0 ? (uint32_t)(-(int32_t)code) : (uint32_t)code);
        a2d_writer_text(&out, ",\""); a2d_writer_text(&out, a2d_error_message(code));
        a2d_writer_text(&out, "\""); a2d_writer_newline(&out);
        if (a2d_transport_end_reply(transport, request->source)) a2d_scpi_errors_pop(&transport->errors);
        break;
    }
    case A2D_CMD_TRIGGER:
        if (!a2d_board_trigger(board)) a2d_transport_error(transport, A2D_ERROR_ADC_CONFIG);
        break;
    case A2D_CMD_LED: a2d_board_set_led(board, request->value != 0); break;
    case A2D_CMD_ADDRESS_SET: a2d_board_set_address(board, request->value); break;
    case A2D_CMD_ADDRESS_SAVE:
        if (!a2d_board_save_address(board)) a2d_transport_error(transport, A2D_ERROR_STORAGE);
        break;
    case A2D_CMD_ADDRESS: {
        A2D_Writer out = a2d_transport_begin_reply(transport, request->source);
        a2d_writer_uint(&out, a2d_board_address(board)); a2d_writer_newline(&out);
        a2d_transport_end_reply(transport, request->source); break;
    }
    case A2D_CMD_VOLTAGE: case A2D_CMD_RAW_VOLTAGE: case A2D_CMD_CALIBRATION:
        read_values(transport, request, channel); break;
    case A2D_CMD_GAIN_QUERY: case A2D_CMD_DATA_RATE_QUERY: case A2D_CMD_MODE_QUERY:
        read_configuration(transport, request, channel); break;
    case A2D_CMD_CALIBRATE:
        if (!a2d_board_calibrate(board, channel - 1, request->points[0], request->points[1], request->points[2], request->points[3]))
            a2d_transport_error(transport, A2D_ERROR_RANGE);
        break;
    case A2D_CMD_CALIBRATION_RESET: case A2D_CMD_CALIBRATION_SAVE:
        if (channel == 0 && request->source == A2D_SOURCE_RS485) break;
        if (command == A2D_CMD_CALIBRATION_RESET) {
            if (!channel) a2d_board_reset_all_calibration(board); else a2d_board_reset_calibration(board, channel - 1);
        } else {
            const bool ok = !channel ? a2d_board_save_all_calibration(board) : a2d_board_save_calibration(board, channel - 1);
            if (!ok) a2d_transport_error(transport, A2D_ERROR_STORAGE);
        }
        break;
    case A2D_CMD_GAIN: case A2D_CMD_DATA_RATE: case A2D_CMD_MODE: {
        uint8_t value = request->value;
        if (command == A2D_CMD_GAIN) value = value == 1 ? MCP3425_GAIN_1 : value == 2 ? MCP3425_GAIN_2 : value == 4 ? MCP3425_GAIN_4 : MCP3425_GAIN_8;
        if (command == A2D_CMD_DATA_RATE) value = value == 15 ? MCP3425_DR_15SPS : value == 60 ? MCP3425_DR_60SPS : MCP3425_DR_240SPS;
        const uint8_t first = channel ? channel - 1 : 0, end = channel ? channel : 4;
        bool ok = true;
        for (uint8_t i = first; i < end; ++i) {
            const bool configured = command == A2D_CMD_GAIN ? a2d_board_set_gain(board, i, value) :
                command == A2D_CMD_DATA_RATE ? a2d_board_set_data_rate(board, i, value) : a2d_board_set_mode(board, i, value);
            if (!configured) ok = false;
        }
        if (!ok) a2d_transport_error(transport, A2D_ERROR_ADC_CONFIG);
        break;
    }
    }
}
