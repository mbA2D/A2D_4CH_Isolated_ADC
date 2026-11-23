#include "A2D_Scpi.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool whitespace(char value) { return value == ' ' || value == '\t' || value == '\r'; }

A2D_ScpiParseResult a2d_scpi_parse(char *line, const A2D_ScpiCommandSpec *table,
                                   size_t count, A2D_ScpiParsedCommand *result) {
    if (!line || !result || (!table && count)) return A2D_SCPI_UNKNOWN_COMMAND;
    /* One command per line. Reject the whole input before accepting any prefix. */
    if (strchr(line, ';') || strchr(line, '\n')) return A2D_SCPI_INVALID_ARGUMENTS;
    char *cursor = line;
    while (whitespace(*cursor)) ++cursor;
    if (!*cursor) return A2D_SCPI_EMPTY;
    char *header = cursor;
    while (*cursor && !whitespace(*cursor)) ++cursor;
    if (*cursor) *cursor++ = '\0';
    const A2D_ScpiCommandSpec *spec = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(header, table[i].name)) { spec = &table[i]; break; }
    }
    if (!spec) return A2D_SCPI_UNKNOWN_COMMAND;
    if (spec->argument_count > A2D_SCPI_MAX_ARGUMENTS) return A2D_SCPI_INVALID_ARGUMENTS;
    const char *query = strchr(header, '?');
    if (query && query[1]) return A2D_SCPI_INVALID_ARGUMENTS;
    A2D_ScpiParsedCommand candidate = {0};
    candidate.spec = spec; candidate.query = query != NULL;
    size_t arguments = 0;
    while (whitespace(*cursor)) ++cursor;
    while (*cursor) {
        if (arguments == A2D_SCPI_MAX_ARGUMENTS) return A2D_SCPI_INVALID_ARGUMENTS;
        char *token = cursor;
        while (*cursor && *cursor != ',') ++cursor;
        char *end = cursor;
        char *next = *cursor ? cursor + 1 : NULL;
        while (end > token && whitespace(end[-1])) --end;
        if (end == token) return A2D_SCPI_INVALID_ARGUMENTS;
        for (char *p = token; p < end; ++p)
            if (whitespace(*p) || *p == '?') return A2D_SCPI_INVALID_ARGUMENTS;
        *end = '\0';
        candidate.arguments[arguments++] = token;
        if (!next) break;
        cursor = next;
        while (whitespace(*cursor)) ++cursor;
        if (!*cursor) return A2D_SCPI_INVALID_ARGUMENTS;
    }
    if (arguments != spec->argument_count) return A2D_SCPI_INVALID_ARGUMENTS;
    *result = candidate;
    return A2D_SCPI_READY;
}

bool a2d_scpi_unsigned_integer(const char *text, uint32_t maximum, uint32_t *value) {
    uint32_t parsed = 0;
    if (!text || !value) return false;
    if (*text == '+') ++text;
    if (!*text) return false;
    for (; *text; ++text) {
        uint32_t digit;
        if (*text < '0' || *text > '9') return false;
        digit = (uint32_t)(*text - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

bool a2d_scpi_real_number(const char *text, float *value) {
    char *end;
    float parsed;
    if (!text || !*text || !value) return false;
    errno = 0;
    parsed = strtof(text, &end);
    if (end == text || *end || errno || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}
