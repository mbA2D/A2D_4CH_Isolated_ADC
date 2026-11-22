#include "A2D_Scpi.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

namespace a2d_scpi {
namespace {
char *next(char *&cursor, const char *delimiters) {
    cursor += strspn(cursor, delimiters);
    if (!*cursor) return nullptr;
    char *token = cursor;
    cursor += strcspn(cursor, delimiters);
    if (*cursor) *cursor++ = '\0';
    return token;
}
}

ParseResult parse(char *line, const CommandSpec *table, size_t count, ParsedCommand &result) {
    if (!line || (!table && count)) return ParseResult::UnknownCommand;
    char *cursor = line;
    const char *header = next(cursor, " \t\r");
    if (!header) return ParseResult::Empty;
    const CommandSpec *spec = nullptr;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(header, table[i].name)) { spec = &table[i]; break; }
    if (!spec) return ParseResult::UnknownCommand;
    if (spec->argumentCount > MaxArguments) return ParseResult::InvalidArguments;

    ParsedCommand candidate = {};
    candidate.spec = spec;
    candidate.query = strchr(header, '?') != nullptr;
    if (char *query = strchr(cursor, '?')) {
        if (!spec->allowTrailingQuery) return ParseResult::InvalidArguments;
        const char *tail = query + 1;
        tail += strspn(tail, " \t\r");
        if (*tail) return ParseResult::InvalidArguments;
        *query = '\0';
        candidate.query = true;
    }
    size_t arguments = 0;
    while (char *token = next(cursor, " ,\t\r")) {
        if (arguments == MaxArguments) return ParseResult::InvalidArguments;
        candidate.arguments[arguments++] = token;
    }
    if (arguments != spec->argumentCount) return ParseResult::InvalidArguments;
    result = candidate;
    return ParseResult::Ready;
}

bool unsignedInteger(const char *text, uint32_t maximum, uint32_t &value) {
    if (!text) return false;
    if (*text == '+') ++text;
    if (!*text) return false;
    uint32_t parsed = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(*text - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool realNumber(const char *text, float &value) {
    if (!text || !*text) return false;
    char *end;
    errno = 0;
    const float parsed = strtof(text, &end);
    if (end == text || *end || errno || !isfinite(parsed)) return false;
    value = parsed;
    return true;
}
}
