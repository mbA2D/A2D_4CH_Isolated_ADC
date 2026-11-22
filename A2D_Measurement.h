#ifndef A2D_MEASUREMENT_H
#define A2D_MEASUREMENT_H
#include <Wire.h>
#include "A2D_MCP3425.h"
namespace a2d {
class Measurement {
public:
    Measurement();
    bool begin(TwoWire *bus);
    bool reset();
    float readRaw(uint8_t channel); // NaN on I2C/configuration/readiness failure.
    bool setGain(uint8_t channel, uint8_t gain);
    bool setRate(uint8_t channel, uint8_t rate);
    bool setMode(uint8_t channel, uint8_t mode);
    bool trigger();
private:
    bool configure(uint8_t channel, uint8_t config);
    bool generalCall(uint8_t command);
    TwoWire *bus_;
    uint8_t config_[4];
    bool configured_[4];
};
}
#endif
