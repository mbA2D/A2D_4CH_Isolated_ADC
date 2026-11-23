#ifndef A2D_SCPI_H
#define A2D_SCPI_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { A2D_SCPI_MAX_ARGUMENTS = 8 };
typedef struct { const char *name; uint16_t id; uint8_t argument_count; } A2D_ScpiCommandSpec;
typedef struct { const A2D_ScpiCommandSpec *spec; const char *arguments[A2D_SCPI_MAX_ARGUMENTS]; bool query; } A2D_ScpiParsedCommand;
typedef enum { A2D_SCPI_READY, A2D_SCPI_EMPTY, A2D_SCPI_UNKNOWN_COMMAND, A2D_SCPI_INVALID_ARGUMENTS } A2D_ScpiParseResult;
/* On success, result borrows caller line/table storage. On failure it is unchanged. */
A2D_ScpiParseResult a2d_scpi_parse(char *line, const A2D_ScpiCommandSpec *table, size_t count, A2D_ScpiParsedCommand *result);
bool a2d_scpi_unsigned_integer(const char *text, uint32_t maximum, uint32_t *value);
bool a2d_scpi_real_number(const char *text, float *value);

#ifdef __cplusplus
}
#endif
#endif
