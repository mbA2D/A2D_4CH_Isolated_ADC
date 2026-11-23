#ifndef A2D_PLATFORM_H
#define A2D_PLATFORM_H
#include "A2D_HAL.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    size_t (*write)(void *context, const uint8_t *data, size_t size);
} A2D_Writer;
typedef struct {
    void *context;
    int (*available)(void *context);
    size_t (*read)(void *context, char *data, size_t size);
    size_t (*write)(void *context, const uint8_t *data, size_t size);
    int (*write_space)(void *context);
    bool (*connected)(void *context);
    void (*drain)(void *context); /* UART TC wait; never a blocking USB flush. */
} A2D_Port;
typedef struct {
    void *context;
    bool (*transmit)(void *context, uint8_t address, uint8_t value);
    bool (*receive)(void *context, uint8_t address, uint8_t *data, size_t size);
} A2D_I2c;
A2D_I2c *a2d_default_i2c_bus(void);
void a2d_writer_text(A2D_Writer *writer, const char *text);
void a2d_writer_uint(A2D_Writer *writer, uint32_t value);
/* Half away from zero,0..5 digits. Invalid/nonfinite/out-of-range inputs emit nothing. */
void a2d_writer_float(A2D_Writer *writer, float value, unsigned digits);
void a2d_writer_newline(A2D_Writer *writer);

#ifdef __cplusplus
}
#endif
#endif
