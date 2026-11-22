#include "Instrument.h"
#include <A2D_Scpi.h>

namespace {
enum Command { Identity, SetOutput, ReadOutput, SetLimit };
const a2d_scpi::CommandSpec commands[] = {
    {"*IDN?", Identity, 0, false},
    {"OUTP:STATE", SetOutput, 1, false},
    {"OUTP:STATE?", ReadOutput, 0, false},
    {"CONF:LIMIT", SetLimit, 1, false}
};
}

void Instrument::execute(char *line, Reply reply, void *context) {
    a2d_scpi::ParsedCommand command;
    const auto result = a2d_scpi::parse(line, commands, sizeof(commands) / sizeof(commands[0]), command);
    if (result == a2d_scpi::ParseResult::Empty) return;
    if (result == a2d_scpi::ParseResult::Ready) {
        switch (command.spec->id) {
        case Identity: reply(context, "Example,Relay,DEMO,1.0\r\n"); return;
        case ReadOutput: reply(context, enabled_ ? "1\r\n" : "0\r\n"); return;
        case SetOutput: {
            uint32_t value;
            if (a2d_scpi::unsignedInteger(command.arguments[0], 1, value)) {
                enabled_ = value != 0;
                return;
            }
            break;
        }
        case SetLimit: {
            float value;
            if (a2d_scpi::realNumber(command.arguments[0], value) && value > 0.0f) {
                limit_ = value;
                return;
            }
            break;
        }
        }
    }
    reply(context, "ERROR:COMMAND\r\n");
}
