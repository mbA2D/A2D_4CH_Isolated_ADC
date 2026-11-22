#ifndef A2D_CALIBRATION_H
#define A2D_CALIBRATION_H
#include <stdint.h>
namespace a2d {
struct Coefficients { float gain; float offset; };
// Device policy, not electrical absolute-maximum ratings. Measured points and
// stored offset use raw ADC volts; actual points use the board's 0..5V input.
namespace CalibrationLimits {
constexpr float ActualMin = 0.0f, ActualMax = 5.0f;
constexpr float MeasuredMax = 2.048f, MinMeasuredSpan = 0.001f;
constexpr float DefaultGain = 2.818181818f;
constexpr float MinGain = DefaultGain * 0.9f, MaxGain = DefaultGain * 1.1f;
constexpr float MaxOffset = 0.25f;
}
class Calibration {
public:
    Calibration();
    void reset(uint8_t channel);
    void resetAll();
    // Protocol order, retained from existing hosts. Measured values are raw ADC volts.
    bool calibrate(uint8_t channel, float actual1, float measured1, float actual2, float measured2);
    static bool calculate(float actual1, float measured1, float actual2, float measured2, Coefficients &values);
    static bool valid(Coefficients values);
    float apply(uint8_t channel, float raw) const;
    Coefficients get(uint8_t channel) const;
    bool load(uint8_t channel, Coefficients values);
private:
    Coefficients values_[4];
};
}
#endif
