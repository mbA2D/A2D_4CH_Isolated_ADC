#ifndef A2D_SCPI_ERROR_QUEUE_H
#define A2D_SCPI_ERROR_QUEUE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { A2D_SCPI_ERROR_CAPACITY = 8, A2D_SCPI_QUEUE_OVERFLOW = -350 };
typedef struct {
    int16_t codes[A2D_SCPI_ERROR_CAPACITY];
    uint8_t head, count;
} A2D_ScpiErrorQueue;
void a2d_scpi_errors_clear(A2D_ScpiErrorQueue *queue);
void a2d_scpi_errors_push(A2D_ScpiErrorQueue *queue, int16_t code);
int16_t a2d_scpi_errors_peek(const A2D_ScpiErrorQueue *queue);
int16_t a2d_scpi_errors_pop(A2D_ScpiErrorQueue *queue);
#ifdef __cplusplus
}
#endif
#endif
