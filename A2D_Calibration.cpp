#include "A2D_Calibration.h"
#include <math.h>
namespace a2d {
Calibration::Calibration() { resetAll(); }
void Calibration::reset(uint8_t channel) {
    if (channel < 4) values_[channel] = {CalibrationLimits::DefaultGain, 0.0f};
}
void Calibration::resetAll() { for (uint8_t i = 0; i < 4; ++i) reset(i); }
bool Calibration::calibrate(uint8_t channel, float actual1, float measured1, float actual2, float measured2) {
    Coefficients values;
    if (channel >= 4 || !calculate(actual1, measured1, actual2, measured2, values)) return false;
    values_[channel] = values;
    return true;
}
bool Calibration::calculate(float actual1, float measured1, float actual2, float measured2, Coefficients &values) {
    using namespace CalibrationLimits;
    if (!isfinite(actual1) || !isfinite(measured1) || !isfinite(actual2) || !isfinite(measured2) ||
        actual1 < ActualMin || actual1 > ActualMax || actual2 < ActualMin || actual2 > ActualMax ||
        fabsf(measured1) > MeasuredMax || fabsf(measured2) > MeasuredMax ||
        actual1 == actual2 || fabsf(measured2 - measured1) < MinMeasuredSpan) return false;
    // Keep the legacy operation order as well as its stored offset convention.
    const float gain = (actual2 - actual1) / (measured2 - measured1);
    const float offset = measured2 - (1 / gain) * actual2;
    if (!valid({gain, offset})) return false;
    values = {gain, offset};
    return true;
}
bool Calibration::valid(Coefficients values) {
    return isfinite(values.gain) && isfinite(values.offset) &&
        values.gain >= CalibrationLimits::MinGain && values.gain <= CalibrationLimits::MaxGain &&
        fabsf(values.offset) <= CalibrationLimits::MaxOffset;
}
float Calibration::apply(uint8_t channel, float raw) const {
    return channel < 4 ? (raw - values_[channel].offset) * values_[channel].gain : 0.0f;
}
Coefficients Calibration::get(uint8_t channel) const {
    return channel < 4 ? values_[channel] : Coefficients{0.0f, 0.0f};
}
bool Calibration::load(uint8_t channel, Coefficients values) {
    if (channel >= 4 || !valid(values)) return false;
    values_[channel] = values;
    return true;
}
}
