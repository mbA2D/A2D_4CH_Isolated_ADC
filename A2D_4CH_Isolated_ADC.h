/* A2D Electronics board API. Retained as a small compatibility facade. */
#ifndef A2D_4CH_Isolated_ADC_h
#define A2D_4CH_Isolated_ADC_h
#include <Arduino.h>
#include <Wire.h>
#include "A2D_4CH_Isolated_ADC_V1.0.h"
#include "A2D_Calibration.h"
#include "A2D_SettingsStore.h"
#include "A2D_Measurement.h"
class A2D_4CH_Isolated_ADC {
public:
    A2D_4CH_Isolated_ADC();
    bool init(TwoWire *i2c = &Wire);
    bool reset();
    float measure_voltage(uint8_t ch);
    float measure_raw_voltage(uint8_t ch);
    // Existing positional API: actual1, measured1, actual2, measured2.
    void calibrate_voltage(uint8_t ch, float actual1, float measured1, float actual2, float measured2);
    bool try_calibrate_voltage(uint8_t ch, float actual1, float measured1, float actual2, float measured2);
    void reset_calibration(uint8_t ch);
    void reset_all_calibration();
    bool save_calibration(uint8_t ch);
    bool save_all_calibration();
    float get_cal_offset(uint8_t ch);
    float get_cal_gain(uint8_t ch);
    bool set_gain(uint8_t ch, uint8_t gain);
    bool set_data_rate(uint8_t ch, uint8_t data_rate);
    bool set_mode(uint8_t ch, uint8_t mode);
    bool trigger_all_single_shot();
    void set_rs485_receive(bool state);
    void set_rs485_addr(uint8_t address);
    bool save_rs485_addr();
    bool reserve_request_ids(uint32_t &first, uint32_t &last);
    uint8_t get_rs485_addr();
    void set_led(bool state);
    char *get_serial_num();
    bool force_eeprom_reinit();
private:
    a2d::Measurement measurement_;
    a2d::Calibration calibration_;
    a2d::SettingsStore storage_;
    uint8_t address_;
    char serial_[A2D_4CH_ISO_ADC_SERIAL_CHAR_LEN];
};
#endif
