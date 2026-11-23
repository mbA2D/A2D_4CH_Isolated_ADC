#ifndef A2D_PROTOCOL_H
#define A2D_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { A2D_LINE_CAPACITY = 200 };
typedef enum { A2D_SOURCE_USB, A2D_SOURCE_RS485 } A2D_Source;
typedef enum {
    A2D_CMD_IDENTITY, A2D_CMD_RESET, A2D_CMD_CLEAR, A2D_CMD_TRIGGER, A2D_CMD_LED,
    A2D_CMD_VOLTAGE, A2D_CMD_RAW_VOLTAGE, A2D_CMD_CALIBRATION_RESET, A2D_CMD_CALIBRATION_SAVE,
    A2D_CMD_CALIBRATE, A2D_CMD_CALIBRATION, A2D_CMD_GAIN, A2D_CMD_DATA_RATE, A2D_CMD_MODE,
    A2D_CMD_ADDRESS_SET, A2D_CMD_ADDRESS, A2D_CMD_ADDRESS_SAVE, A2D_CMD_BOOTLOADER, A2D_CMD_SERIAL_SET,
    A2D_CMD_GAIN_QUERY, A2D_CMD_DATA_RATE_QUERY, A2D_CMD_MODE_QUERY, A2D_CMD_ERROR
} A2D_Command;
typedef struct {
    A2D_Command command; A2D_Source source;
    uint8_t channel, value; uint16_t serial;
    float points[4]; /* ACTUAL1, MEASURED1, ACTUAL2, MEASURED2 */
    char original[A2D_LINE_CAPACITY];
} A2D_Request;
typedef enum {
    A2D_ERROR_SYNTAX = -102, A2D_ERROR_UNDEFINED_HEADER = -113,
    A2D_ERROR_NUMERIC_DATA = -128, A2D_ERROR_EXECUTION = -200, A2D_ERROR_RANGE = -222,
    A2D_ERROR_ILLEGAL_VALUE = -224, A2D_ERROR_STORAGE = -320,
    A2D_ERROR_INPUT_OVERRUN = -363, A2D_ERROR_QUERY = -400,
    A2D_ERROR_ADC_READ = 1001, A2D_ERROR_ADC_CONFIG = 1002,
    A2D_ERROR_IDENTITY_LOCKED = 1003, A2D_ERROR_RESET = 1004,
    A2D_ERROR_RS485_TIMEOUT = 1101, A2D_ERROR_RS485_WRITE = 1102,
    A2D_ERROR_RS485_BUSY = 1103
} A2D_Error;
typedef enum {
    A2D_PARSE_READY, A2D_PARSE_IGNORE,
    A2D_PARSE_INVALID_COMMAND = A2D_ERROR_UNDEFINED_HEADER,
    A2D_PARSE_INVALID_ARGUMENTS = A2D_ERROR_SYNTAX,
    A2D_PARSE_INVALID_NUMBER = A2D_ERROR_NUMERIC_DATA,
    A2D_PARSE_NOT_ALLOWED = A2D_ERROR_EXECUTION,
    A2D_PARSE_OUT_OF_RANGE = A2D_ERROR_RANGE,
    A2D_PARSE_ILLEGAL_VALUE = A2D_ERROR_ILLEGAL_VALUE
} A2D_ParseResult;
const char *a2d_error_message(int16_t code);
A2D_ParseResult a2d_parse(const char *line, A2D_Source source, uint8_t address, A2D_Request *request);
bool a2d_channel_on_device(uint8_t channel, uint8_t address);
uint8_t a2d_local_channel(uint8_t channel, uint8_t address);
uint8_t a2d_channel_address(uint8_t channel); /* Requires channel1..128. */

#ifdef __cplusplus
}
#endif
#endif
