#include "A2D_Calibration.h"
#include <math.h>
void a2d_calibration_init(A2D_Calibration *calibration) {
    a2d_calibration_reset_all(calibration);
}
void a2d_calibration_reset(A2D_Calibration *calibration, uint8_t channel) {
    if (channel < 4) calibration->values[channel] = (A2D_Coefficients){A2D_CAL_DEFAULT_GAIN, 0.0f};
}
void a2d_calibration_reset_all(A2D_Calibration *calibration) {
    for (uint8_t i = 0; i < 4; ++i) a2d_calibration_reset(calibration, i);
}
bool a2d_calibration_valid(A2D_Coefficients values) {
    return isfinite(values.gain) && isfinite(values.offset) &&
        values.gain >= A2D_CAL_MIN_GAIN && values.gain <= A2D_CAL_MAX_GAIN &&
        fabsf(values.offset) <= A2D_CAL_MAX_OFFSET;
}
bool a2d_calibration_calculate(float actual1, float measured1, float actual2, float measured2, A2D_Coefficients *values) {
    if (!isfinite(actual1) || !isfinite(measured1) || !isfinite(actual2) || !isfinite(measured2) ||
        actual1 < A2D_CAL_ACTUAL_MIN || actual1 > A2D_CAL_ACTUAL_MAX ||
        actual2 < A2D_CAL_ACTUAL_MIN || actual2 > A2D_CAL_ACTUAL_MAX ||
        fabsf(measured1) > A2D_CAL_MEASURED_MAX || fabsf(measured2) > A2D_CAL_MEASURED_MAX ||
        actual1 == actual2 || fabsf(measured2 - measured1) < A2D_CAL_MIN_SPAN) return false;
    /* Preserve operation order and raw-voltage offset convention. */
    const float gain = (actual2 - actual1) / (measured2 - measured1);
    const float offset = measured2 - (1 / gain) * actual2;
    const A2D_Coefficients candidate = {gain, offset};
    if (!a2d_calibration_valid(candidate)) return false;
    *values = candidate;
    return true;
}
bool a2d_calibration_calibrate(A2D_Calibration *calibration, uint8_t channel, float actual1, float measured1, float actual2, float measured2) {
    A2D_Coefficients values;
    if (channel >= 4 || !a2d_calibration_calculate(actual1, measured1, actual2, measured2, &values)) return false;
    calibration->values[channel] = values;
    return true;
}
float a2d_calibration_apply(const A2D_Calibration *calibration, uint8_t channel, float raw) {
    return channel < 4 ? (raw - calibration->values[channel].offset) * calibration->values[channel].gain : NAN;
}
A2D_Coefficients a2d_calibration_get(const A2D_Calibration *calibration, uint8_t channel) {
    return channel < 4 ? calibration->values[channel] : (A2D_Coefficients){0.0f, 0.0f};
}
bool a2d_calibration_load(A2D_Calibration *calibration, uint8_t channel, A2D_Coefficients values) {
    if (channel >= 4 || !a2d_calibration_valid(values)) return false;
    calibration->values[channel] = values;
    return true;
}
