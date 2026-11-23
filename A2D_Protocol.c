#include "A2D_Protocol.h"
#include "A2D_Calibration.h"
#include <A2D_Scpi.h>
#include <string.h>
static A2D_ParseResult number(const char *text, uint8_t maximum, uint8_t *value) {
    const char *digits = text;
    if (*digits == '+' || *digits == '-') ++digits;
    if (!*digits || strspn(digits, "0123456789") != strlen(digits)) return A2D_PARSE_INVALID_NUMBER;
    uint32_t parsed;
    if (*text == '-' || !a2d_scpi_unsigned_integer(text, maximum, &parsed)) return A2D_PARSE_OUT_OF_RANGE;
    *value = (uint8_t)parsed; return A2D_PARSE_READY;
}
#define SPEC(name, command, arguments) {name, A2D_CMD_##command, arguments}
static const A2D_ScpiCommandSpec specifications[] = {
    SPEC("*IDN?", IDENTITY, 0), SPEC("*RST", RESET, 0),
    SPEC("*CLS", CLEAR, 0), SPEC("SYST:ERR?", ERROR, 0), SPEC("*TRG", TRIGGER, 0),
    SPEC("INSTR:LED", LED, 1), SPEC("MEAS:VOLT?", VOLTAGE, 1),
    SPEC("MEAS:VOLT:ADC?", RAW_VOLTAGE, 1), SPEC("CAL:RESET", CALIBRATION_RESET, 1),
    SPEC("CAL:SAVE", CALIBRATION_SAVE, 1), SPEC("CAL:VOLT", CALIBRATE, 5),
    SPEC("CAL?", CALIBRATION, 1), SPEC("INSTR:GAIN", GAIN, 2),
    SPEC("INSTR:DR", DATA_RATE, 2), SPEC("INSTR:MODE", MODE, 2),
    SPEC("INSTR:RS485:SET", ADDRESS_SET, 1), SPEC("INSTR:RS485?", ADDRESS, 0),
    SPEC("INSTR:RS485:SAVE", ADDRESS_SAVE, 0), SPEC("SYST:BOOT", BOOTLOADER, 0),
    SPEC("SYST:SER:SET", SERIAL_SET, 1),
    SPEC("INSTR:GAIN?", GAIN_QUERY, 1), SPEC("INSTR:DR?", DATA_RATE_QUERY, 1),
    SPEC("INSTR:MODE?", MODE_QUERY, 1)
};
#undef SPEC
bool a2d_channel_on_device(uint8_t channel, uint8_t address) {
    return channel >= address * 4 + 1 && channel <= address * 4 + 4;
}
uint8_t a2d_local_channel(uint8_t channel, uint8_t address) {
    return a2d_channel_on_device(channel, address) ? (uint8_t)((channel - 1) % 4 + 1) : channel;
}
uint8_t a2d_channel_address(uint8_t channel) { return (uint8_t)((channel - 1) / 4); }
A2D_ParseResult a2d_parse(const char *line, A2D_Source source, uint8_t address, A2D_Request *request) {
    if (strlen(line) >= A2D_LINE_CAPACITY) return A2D_PARSE_INVALID_COMMAND;
    char buffer[A2D_LINE_CAPACITY]; strcpy(buffer, line);
    const char *header = line + strspn(line, " \t\r");
    if (strcspn(header, " \t\r") >= 32) return A2D_PARSE_INVALID_COMMAND;
    A2D_ScpiParsedCommand parsed;
    switch (a2d_scpi_parse(buffer, specifications, sizeof(specifications) / sizeof(specifications[0]), &parsed)) {
    case A2D_SCPI_EMPTY: return A2D_PARSE_IGNORE;
    case A2D_SCPI_UNKNOWN_COMMAND: return A2D_PARSE_INVALID_COMMAND;
    case A2D_SCPI_INVALID_ARGUMENTS: return A2D_PARSE_INVALID_ARGUMENTS;
    case A2D_SCPI_READY: break;
    }
    const char *const *arguments = parsed.arguments;
    A2D_Request candidate = {0};
    candidate.command = (A2D_Command)parsed.spec->id; candidate.source = source;
    if ((candidate.command == A2D_CMD_BOOTLOADER || candidate.command == A2D_CMD_SERIAL_SET) && source != A2D_SOURCE_USB)
        return A2D_PARSE_NOT_ALLOWED;
    int8_t channel_index = -1;
    switch (candidate.command) {
    case A2D_CMD_VOLTAGE: case A2D_CMD_RAW_VOLTAGE: case A2D_CMD_CALIBRATION_RESET:
    case A2D_CMD_CALIBRATION_SAVE: case A2D_CMD_CALIBRATE: case A2D_CMD_CALIBRATION:
    case A2D_CMD_GAIN_QUERY: case A2D_CMD_DATA_RATE_QUERY: case A2D_CMD_MODE_QUERY:
        channel_index = 0; break;
    case A2D_CMD_GAIN: case A2D_CMD_DATA_RATE: case A2D_CMD_MODE:
        channel_index = 1; break;
    default: break;
    }
    if (channel_index >= 0) {
        const A2D_ParseResult valid = number(arguments[channel_index], 128, &candidate.channel);
        if (valid != A2D_PARSE_READY) return valid;
        if (source == A2D_SOURCE_RS485 && candidate.channel && !a2d_channel_on_device(candidate.channel, address))
            return A2D_PARSE_OUT_OF_RANGE;
    }
    switch (candidate.command) {
    case A2D_CMD_SERIAL_SET: {
        uint32_t serial;
        if (strlen(arguments[0]) != 4 || strspn(arguments[0], "0123456789") != 4 ||
            !a2d_scpi_unsigned_integer(arguments[0], 9999, &serial)) return A2D_PARSE_INVALID_ARGUMENTS;
        candidate.serial = (uint16_t)serial; break;
    }
    case A2D_CMD_LED:
        { const A2D_ParseResult valid = number(arguments[0], 1, &candidate.value);
          if (valid != A2D_PARSE_READY) return valid; }
        break;
    case A2D_CMD_ADDRESS_SET:
        { const A2D_ParseResult valid = number(arguments[0], 31, &candidate.value);
          if (valid != A2D_PARSE_READY) return valid; }
        break;
    case A2D_CMD_GAIN: case A2D_CMD_DATA_RATE: case A2D_CMD_MODE:
        { const A2D_ParseResult valid = number(arguments[0], 240, &candidate.value);
          if (valid != A2D_PARSE_READY) return valid; }
        if (candidate.command == A2D_CMD_GAIN && candidate.value != 1 && candidate.value != 2 && candidate.value != 4 && candidate.value != 8)
            return A2D_PARSE_ILLEGAL_VALUE;
        if (candidate.command == A2D_CMD_DATA_RATE && candidate.value != 15 && candidate.value != 60 && candidate.value != 240)
            return A2D_PARSE_ILLEGAL_VALUE;
        if (candidate.command == A2D_CMD_MODE && candidate.value > 1) return A2D_PARSE_ILLEGAL_VALUE;
        break;
    case A2D_CMD_CALIBRATE: {
        if (!candidate.channel) return A2D_PARSE_OUT_OF_RANGE;
        for (uint8_t i = 0; i < 4; ++i)
            if (!a2d_scpi_real_number(arguments[i + 1], &candidate.points[i])) return A2D_PARSE_INVALID_NUMBER;
        A2D_Coefficients coefficients;
        if (!a2d_calibration_calculate(candidate.points[0], candidate.points[1], candidate.points[2], candidate.points[3], &coefficients))
            return A2D_PARSE_OUT_OF_RANGE;
        break;
    }
    default: break;
    }
    strcpy(candidate.original, line); *request = candidate; return A2D_PARSE_READY;
}

const char *a2d_error_message(int16_t code) {
    switch (code) {
    case 0: return "No error";
    case A2D_ERROR_SYNTAX: return "Syntax error";
    case A2D_ERROR_UNDEFINED_HEADER: return "Undefined header";
    case A2D_ERROR_NUMERIC_DATA: return "Numeric data error";
    case A2D_ERROR_EXECUTION: return "Execution error";
    case A2D_ERROR_RANGE: return "Data out of range";
    case A2D_ERROR_ILLEGAL_VALUE: return "Illegal parameter value";
    case A2D_ERROR_STORAGE: return "Storage fault";
    case -350: return "Queue overflow";
    case A2D_ERROR_INPUT_OVERRUN: return "Input buffer overrun";
    case A2D_ERROR_QUERY: return "Query error";
    case A2D_ERROR_ADC_READ: return "ADC read failed";
    case A2D_ERROR_ADC_CONFIG: return "ADC configuration failed";
    case A2D_ERROR_IDENTITY_LOCKED: return "Identity locked";
    case A2D_ERROR_RESET: return "Settings reload failed";
    case A2D_ERROR_RS485_TIMEOUT: return "RS485 timeout";
    case A2D_ERROR_RS485_WRITE: return "RS485 write failed";
    case A2D_ERROR_RS485_BUSY: return "RS485 busy";
    default: return "Device-specific error";
    }
}
