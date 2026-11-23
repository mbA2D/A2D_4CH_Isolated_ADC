#ifndef A2D_MEASUREMENT_H
#define A2D_MEASUREMENT_H
#include "A2D_Platform.h"
#include "A2D_MCP3425.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { A2D_I2c *bus; uint8_t config[4]; bool configured[4]; } A2D_Measurement;
bool a2d_measurement_begin(A2D_Measurement *measurement, A2D_I2c *bus);
bool a2d_measurement_reset(A2D_Measurement *measurement);
/* Returns NaN on I2C, configuration or readiness failure. */
float a2d_measurement_read_raw(A2D_Measurement *measurement, uint8_t channel);
bool a2d_measurement_set_gain(A2D_Measurement *measurement, uint8_t channel, uint8_t gain);
bool a2d_measurement_set_rate(A2D_Measurement *measurement, uint8_t channel, uint8_t rate);
bool a2d_measurement_set_mode(A2D_Measurement *measurement, uint8_t channel, uint8_t mode);
bool a2d_measurement_trigger(A2D_Measurement *measurement);
/* Read/confirm echoed hardware config without waiting for conversion or writing.
   Returns low5 configuration bits; ignores RDY. Output changes only on success. */
bool a2d_measurement_get_configuration(A2D_Measurement *measurement, uint8_t channel, uint8_t *configuration);

#ifdef __cplusplus
}
#endif
#endif
