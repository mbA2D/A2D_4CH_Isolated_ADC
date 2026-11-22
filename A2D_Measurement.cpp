#include "A2D_Measurement.h"
#include "A2D_4CH_Isolated_ADC_V1.0.h"
#include <Arduino.h>
#include <math.h>
namespace a2d {
namespace {
const uint8_t addresses[] = {A2D_4CH_ISO_ADC_CH1_I2C_ADDR, A2D_4CH_ISO_ADC_CH2_I2C_ADDR,
                            A2D_4CH_ISO_ADC_CH3_I2C_ADDR, A2D_4CH_ISO_ADC_CH4_I2C_ADDR};
}
Measurement::Measurement() : bus_(&Wire), config_{}, configured_{} {}
bool Measurement::begin(TwoWire *bus) { bus_ = bus; return reset(); }
bool Measurement::generalCall(uint8_t command) {
    bus_->beginTransmission(0);
    const bool sent = bus_->write(command) == 1;
    return bus_->endTransmission(true) == 0 && sent;
}
bool Measurement::configure(uint8_t channel, uint8_t config) {
    bus_->beginTransmission(addresses[channel]);
    // RDY starts a new conversion in single-shot mode, and is ignored in continuous mode.
    const bool sent = bus_->write(config | 0x80) == 1;
    configured_[channel] = bus_->endTransmission(true) == 0 && sent;
    if (configured_[channel]) config_[channel] = config;
    return configured_[channel];
}
bool Measurement::reset() {
    bool ok = generalCall(0x06); // One bus-wide reset before any channel is configured.
    for (uint8_t i = 0; i < 4; ++i) {
        config_[i] = 0x18; // gain1, 15SPS, continuous
        configured_[i] = false;
        if (!configure(i, config_[i])) ok = false;
    }
    return ok;
}
float Measurement::readRaw(uint8_t channel) {
    if (channel >= 4 || !configured_[channel]) return NAN;
    const uint32_t start = static_cast<uint32_t>(millis());
    do {
        if (bus_->requestFrom(addresses[channel], static_cast<uint8_t>(3)) != 3) return NAN;
        const int high = bus_->read(), low = bus_->read(), config = bus_->read();
        if (high < 0 || low < 0 || config < 0 || (config & 0x1f) != config_[channel]) return NAN;
        if (!(config & 0x80)) {
            int32_t code = (high << 8) | low;
            if (code & 0x8000) code -= 65536;
            const uint8_t rate = (config_[channel] >> 2) & 3;
            code *= 1 << (2 * (2 - rate)); // Sign-safe normalization of 12/14/16-bit results.
            const unsigned gain = 1U << (config_[channel] & 3);
            return static_cast<float>(code) * (2.048f / (32768.0f * gain));
        }
        delay(1);
    } while (static_cast<uint32_t>(static_cast<uint32_t>(millis()) - start) < 120U);
    return NAN; // Never reuse a conversion from a previous gain/configuration/reset.
}
bool Measurement::setGain(uint8_t channel, uint8_t gain) {
    return channel < 4 && gain <= 3 && configure(channel, (config_[channel] & ~3U) | gain);
}
bool Measurement::setRate(uint8_t channel, uint8_t rate) {
    return channel < 4 && rate <= 2 && configure(channel, (config_[channel] & ~12U) | (rate << 2));
}
bool Measurement::setMode(uint8_t channel, uint8_t mode) {
    return channel < 4 && mode <= 1 && configure(channel, (config_[channel] & ~16U) | (mode << 4));
}
bool Measurement::trigger() {
    const bool ok = generalCall(0x08);
    for (uint8_t i = 0; i < 4; ++i) {
        config_[i] &= ~16U; // General-call conversion forces single-shot mode.
        if (!ok) configured_[i] = false;
    }
    return ok;
}
}
