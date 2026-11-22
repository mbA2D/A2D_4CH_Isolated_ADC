#ifndef A2D_SCPI_H
#define A2D_SCPI_H

#include <stddef.h>
#include <stdint.h>

namespace a2d_scpi {

constexpr size_t MaxArguments = 8;

// Names match exactly, including any header '?'. IDs belong to the application.
struct CommandSpec {
    const char *name;
    uint16_t id;
    uint8_t argumentCount;
    bool allowTrailingQuery; // Also accept '?' after the last argument.
};

struct ParsedCommand {
    const CommandSpec *spec;
    const char *arguments[MaxArguments];
    bool query;
};

enum class ParseResult { Ready, Empty, UnknownCommand, InvalidArguments };

// Tokenizes a writable, NUL-terminated line WITHOUT its LF terminator in place.
// On Ready, result borrows line and table storage; consume it before either is
// changed/destroyed. On failure, result is unchanged; line may be modified.
// Every table name must be unique, with argumentCount <= MaxArguments.
ParseResult parse(char *line, const CommandSpec *table, size_t count, ParsedCommand &result);

// Full-token conversion. Outputs change only on success. Integers are decimal
// (optional '+'); floats use strtof syntax and must be finite and in range.
bool unsignedInteger(const char *text, uint32_t maximum, uint32_t &value);
bool realNumber(const char *text, float &value);

}
#endif
