#include "Instrument.h"
#include <A2D_Scpi.h>
#include <stdio.h>

enum Command { Identity, SetOutput, ReadOutput, SetLimit, Clear, ReadError };
static const A2D_ScpiCommandSpec commands[] = {
    {"*IDN?", Identity, 0},
    {"OUTP:STATE", SetOutput, 1},
    {"OUTP:STATE?", ReadOutput, 0},
    {"CONF:LIMIT", SetLimit, 1}, {"*CLS", Clear, 0}, {"SYST:ERR?", ReadError, 0}
};

void instrument_init(Instrument *instrument) {
    instrument->enabled = false;
    instrument->limit = 1.0f;
    a2d_scpi_errors_clear(&instrument->errors);
}

void instrument_execute(Instrument *instrument, char *line, InstrumentReply reply, void *context) {
    A2D_ScpiParsedCommand command;
    const A2D_ScpiParseResult result = a2d_scpi_parse(
        line, commands, sizeof(commands) / sizeof(commands[0]), &command);
    if (result == A2D_SCPI_EMPTY) return;
    if (result == A2D_SCPI_READY) {
        switch (command.spec->id) {
        case Clear: a2d_scpi_errors_clear(&instrument->errors); return;
        case ReadError: {
            const int code = a2d_scpi_errors_pop(&instrument->errors);
            const char *text = code == 0 ? "No error" : code == -113 ? "Undefined header" :
                code == -102 ? "Syntax error" : code == -350 ? "Queue overflow" : "Data out of range";
            char response[64];
            snprintf(response, sizeof(response), "%d,\"%s\"\r\n", code, text);
            reply(context, response); return;
        }
        case Identity: reply(context, "Example,Relay,DEMO,1.0\r\n"); return;
        case ReadOutput: reply(context, instrument->enabled ? "1\r\n" : "0\r\n"); return;
        case SetOutput: {
            uint32_t value;
            if (a2d_scpi_unsigned_integer(command.arguments[0], 1, &value)) {
                instrument->enabled = value != 0;
                return;
            }
            break;
        }
        case SetLimit: {
            float value;
            if (a2d_scpi_real_number(command.arguments[0], &value) && value > 0.0f) {
                instrument->limit = value;
                return;
            }
            break;
        }
        }
    }
    a2d_scpi_errors_push(&instrument->errors, result == A2D_SCPI_UNKNOWN_COMMAND ? -113 :
                         result == A2D_SCPI_READY ? -222 : -102);
}
