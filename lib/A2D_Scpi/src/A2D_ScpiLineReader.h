#ifndef A2D_SCPILINEREADER_H
#define A2D_SCPILINEREADER_H
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { A2D_SCPI_LINE_BUDGET = 64 };
typedef enum { A2D_SCPI_LINE_PENDING, A2D_SCPI_LINE_COMPLETE, A2D_SCPI_LINE_TOO_LONG } A2D_ScpiLineResult;
typedef struct { char *data; size_t capacity, length; bool overflow; } A2D_ScpiLineBuffer;
typedef struct { A2D_ScpiLineBuffer line; char pending[A2D_SCPI_LINE_BUDGET]; size_t position, count; } A2D_ScpiLineReader;
typedef struct { void *context; int (*available)(void *context); size_t (*read)(void *context, char *data, size_t size); } A2D_ScpiInput;
/* Caller owns storage for the life of the context; capacity includes NUL. */
bool a2d_scpi_line_init(A2D_ScpiLineBuffer *line, char *storage, size_t capacity);
void a2d_scpi_line_reset(A2D_ScpiLineBuffer *line);
A2D_ScpiLineResult a2d_scpi_line_push(A2D_ScpiLineBuffer *line, char value);
bool a2d_scpi_reader_init(A2D_ScpiLineReader *reader, char *storage, size_t capacity);
void a2d_scpi_reader_reset(A2D_ScpiLineReader *reader);
bool a2d_scpi_reader_has_pending(const A2D_ScpiLineReader *reader);
/* Nonblocking input callbacks return <=requested bytes. output has capacity bytes. */
int a2d_scpi_reader_poll(A2D_ScpiLineReader *reader, const A2D_ScpiInput *input, char *output);

#ifdef __cplusplus
}
#endif
#endif
