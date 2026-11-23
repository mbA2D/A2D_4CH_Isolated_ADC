#include "A2D_ScpiErrorQueue.h"
#include <stddef.h>
void a2d_scpi_errors_clear(A2D_ScpiErrorQueue *queue) {
    if (queue) { queue->head = 0; queue->count = 0; }
}
void a2d_scpi_errors_push(A2D_ScpiErrorQueue *queue, int16_t code) {
    if (!queue || !code) return;
    if (queue->count == A2D_SCPI_ERROR_CAPACITY) {
        queue->codes[(queue->head + queue->count - 1) % A2D_SCPI_ERROR_CAPACITY] = A2D_SCPI_QUEUE_OVERFLOW;
    } else {
        queue->codes[(queue->head + queue->count) % A2D_SCPI_ERROR_CAPACITY] = code;
        ++queue->count;
    }
}
int16_t a2d_scpi_errors_peek(const A2D_ScpiErrorQueue *queue) {
    return queue && queue->count ? queue->codes[queue->head] : 0;
}
int16_t a2d_scpi_errors_pop(A2D_ScpiErrorQueue *queue) {
    const int16_t code = a2d_scpi_errors_peek(queue);
    if (queue && queue->count) {
        queue->head = (uint8_t)((queue->head + 1) % A2D_SCPI_ERROR_CAPACITY);
        --queue->count;
    }
    return code;
}
