#ifndef A2D_CALIBRATION_H
#define A2D_CALIBRATION_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float gain, offset; } A2D_Coefficients;
typedef struct { A2D_Coefficients values[4]; } A2D_Calibration;
#define A2D_CAL_ACTUAL_MIN 0.0f
#define A2D_CAL_ACTUAL_MAX 5.0f
#define A2D_CAL_MEASURED_MAX 2.048f
#define A2D_CAL_MIN_SPAN 0.001f
#define A2D_CAL_DEFAULT_GAIN 2.818181818f
#define A2D_CAL_MIN_GAIN (A2D_CAL_DEFAULT_GAIN * 0.9f)
#define A2D_CAL_MAX_GAIN (A2D_CAL_DEFAULT_GAIN * 1.1f)
#define A2D_CAL_MAX_OFFSET 0.25f
/* Offset/measured points are raw ADC volts; actual points are board-input volts. */
void a2d_calibration_init(A2D_Calibration *calibration);
void a2d_calibration_reset(A2D_Calibration *calibration, uint8_t channel);
void a2d_calibration_reset_all(A2D_Calibration *calibration);
bool a2d_calibration_valid(A2D_Coefficients values);
bool a2d_calibration_calculate(float actual1, float measured1, float actual2, float measured2, A2D_Coefficients *values);
bool a2d_calibration_calibrate(A2D_Calibration *calibration, uint8_t channel, float actual1, float measured1, float actual2, float measured2);
/* NaN on invalid channel, preserving a failed acquisition rather than inventing0 V. */
float a2d_calibration_apply(const A2D_Calibration *calibration, uint8_t channel, float raw);
A2D_Coefficients a2d_calibration_get(const A2D_Calibration *calibration, uint8_t channel);
bool a2d_calibration_load(A2D_Calibration *calibration, uint8_t channel, A2D_Coefficients values);

#ifdef __cplusplus
}
#endif
#endif
