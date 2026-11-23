#include "A2D_Measurement.h"
#include "A2D_BoardConfig.h"
#include <math.h>
static const uint8_t addresses[4] = {
    A2D_4CH_ISO_ADC_CH1_I2C_ADDR, A2D_4CH_ISO_ADC_CH2_I2C_ADDR,
    A2D_4CH_ISO_ADC_CH3_I2C_ADDR, A2D_4CH_ISO_ADC_CH4_I2C_ADDR
};
static bool general_call(A2D_Measurement *measurement, uint8_t command) {
    return measurement->bus->transmit(measurement->bus->context, 0, command);
}
static bool configure(A2D_Measurement *measurement, uint8_t channel, uint8_t config) {
    measurement->configured[channel] = measurement->bus->transmit(measurement->bus->context, addresses[channel], config | 0x80);
    if (measurement->configured[channel]) measurement->config[channel] = config;
    return measurement->configured[channel];
}
bool a2d_measurement_begin(A2D_Measurement *measurement, A2D_I2c *bus) {
    measurement->bus = bus ? bus : a2d_default_i2c_bus();
    return a2d_measurement_reset(measurement);
}
bool a2d_measurement_reset(A2D_Measurement *measurement) {
    bool ok = general_call(measurement, 0x06);
    for (uint8_t i = 0; i < 4; ++i) {
        measurement->config[i] = 0x18; /* gain1, 15SPS, continuous */
        measurement->configured[i] = false;
        if (!configure(measurement, i, measurement->config[i])) ok = false;
    }
    return ok;
}
float a2d_measurement_read_raw(A2D_Measurement *measurement, uint8_t channel) {
    if (channel >= 4 || !measurement->configured[channel]) return NAN;
    const uint32_t start = A2D_millis();
    do {
        uint8_t data[3];
        if (!measurement->bus->receive(measurement->bus->context, addresses[channel], data, sizeof(data))) return NAN;
        const int high = data[0], low = data[1], config = data[2];
        if ((config & 0x1f) != measurement->config[channel]) return NAN;
        if (!(config & 0x80)) {
            int32_t code = (high << 8) | low;
            if (code & 0x8000) code -= 65536;
            const uint8_t rate = (measurement->config[channel] >> 2) & 3;
            code *= 1 << (2 * (2 - rate));
            const unsigned gain = 1U << (measurement->config[channel] & 3);
            return (float)code * (2.048f / (32768.0f * gain));
        }
        A2D_delay(1);
    } while ((uint32_t)(A2D_millis() - start) < 120U);
    return NAN;
}
bool a2d_measurement_set_gain(A2D_Measurement *measurement, uint8_t channel, uint8_t gain) {
    return channel < 4 && gain <= 3 && configure(measurement, channel, (measurement->config[channel] & ~3U) | gain);
}
bool a2d_measurement_set_rate(A2D_Measurement *measurement, uint8_t channel, uint8_t rate) {
    return channel < 4 && rate <= 2 && configure(measurement, channel, (measurement->config[channel] & ~12U) | (rate << 2));
}
bool a2d_measurement_set_mode(A2D_Measurement *measurement, uint8_t channel, uint8_t mode) {
    return channel < 4 && mode <= 1 && configure(measurement, channel, (measurement->config[channel] & ~16U) | (mode << 4));
}
bool a2d_measurement_trigger(A2D_Measurement *measurement) {
    const bool ok = general_call(measurement, 0x08);
    for (uint8_t i = 0; i < 4; ++i) {
        measurement->config[i] &= ~16U;
        if (!ok) measurement->configured[i] = false;
    }
    return ok;
}

bool a2d_measurement_get_configuration(A2D_Measurement *measurement, uint8_t channel, uint8_t *configuration) {
    if (channel >= 4 || !measurement->configured[channel]) return false;
    uint8_t data[3];
    if (!measurement->bus->receive(measurement->bus->context, addresses[channel], data, sizeof(data))) return false;
    const uint8_t config = data[2] & 0x1f;
    if (config != measurement->config[channel] || ((config >> 2) & 3) > 2) return false;
    *configuration = config;
    return true;
}
