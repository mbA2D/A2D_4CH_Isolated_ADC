#include "A2D_ScpiLineReader.h"
#include <string.h>

bool a2d_scpi_line_init(A2D_ScpiLineBuffer *line, char *storage, size_t capacity) {
    if (!line || !storage || capacity < 2) return false;
    line->data = storage;
    line->capacity = capacity;
    a2d_scpi_line_reset(line);
    return true;
}

void a2d_scpi_line_reset(A2D_ScpiLineBuffer *line) {
    if (!line || !line->data || line->capacity < 2) return;
    line->length = 0;
    line->overflow = false;
    line->data[0] = '\0';
}

A2D_ScpiLineResult a2d_scpi_line_push(A2D_ScpiLineBuffer *line, char value) {
    if (!line || !line->data || line->capacity < 2) return A2D_SCPI_LINE_TOO_LONG;
    if (value == '\n') {
        if (line->overflow) {
            a2d_scpi_line_reset(line);
            return A2D_SCPI_LINE_TOO_LONG;
        }
        if (line->length && line->data[line->length - 1] == '\r') --line->length;
        line->data[line->length] = '\0';
        return A2D_SCPI_LINE_COMPLETE;
    }
    if (!line->overflow) {
        /* Reject NULs and discard an invalid frame through its next LF. */
        if (value == '\0' || line->length == line->capacity - 1) line->overflow = true;
        else line->data[line->length++] = value;
    }
    return A2D_SCPI_LINE_PENDING;
}

bool a2d_scpi_reader_init(A2D_ScpiLineReader *reader, char *storage, size_t capacity) {
    if (!reader || !a2d_scpi_line_init(&reader->line, storage, capacity)) return false;
    reader->position = reader->count = 0;
    return true;
}

void a2d_scpi_reader_reset(A2D_ScpiLineReader *reader) {
    if (!reader) return;
    a2d_scpi_line_reset(&reader->line);
    reader->position = reader->count = 0;
}

bool a2d_scpi_reader_has_pending(const A2D_ScpiLineReader *reader) {
    return reader && reader->position < reader->count;
}

int a2d_scpi_reader_poll(A2D_ScpiLineReader *reader, const A2D_ScpiInput *input,
                         char *output) {
    if (!reader || !reader->line.data || reader->line.capacity < 2 ||
        !input || !input->available || !input->read || !output) return -1;
    for (size_t consumed = 0; consumed < A2D_SCPI_LINE_BUDGET; ++consumed) {
        A2D_ScpiLineResult result;
        if (reader->position == reader->count) {
            const int available = input->available(input->context);
            size_t wanted;
            if (available <= 0) return 0;
            wanted = (size_t)available < sizeof(reader->pending) ?
                (size_t)available : sizeof(reader->pending);
            reader->count = input->read(input->context, reader->pending, wanted);
            reader->position = 0;
            if (!reader->count) return 0;
        }
        result = a2d_scpi_line_push(&reader->line, reader->pending[reader->position++]);
        if (result == A2D_SCPI_LINE_TOO_LONG) return -1;
        if (result == A2D_SCPI_LINE_COMPLETE) {
            memcpy(output, reader->line.data, reader->line.length + 1);
            a2d_scpi_line_reset(&reader->line);
            return 1;
        }
    }
    return 0;
}
