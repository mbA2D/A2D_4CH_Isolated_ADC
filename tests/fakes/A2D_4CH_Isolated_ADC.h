#ifndef TEST_A2D_ADC_H
#define TEST_A2D_ADC_H

#include "Arduino.h"
#include <math.h>

#define MANUFACTURER "A2D Electronics"
#define MODEL "4CH Isolated ADC"
#define VERSION "V2.1.3"
#define A2D_4CH_ISO_ADC_NUM_CHANNELS 4
#define A2D_4CH_ISO_ADC_BASE_RS485_ADDR 0
#define A2D_4CH_ISO_ADC_CMD_SOURCE_USB 1
#define A2D_4CH_ISO_ADC_CMD_SOURCE_RS485 2
#define MCP3425_GAIN_1 0
#define MCP3425_GAIN_2 1
#define MCP3425_GAIN_4 2
#define MCP3425_GAIN_8 3
#define MCP3425_DR_240SPS 0
#define MCP3425_DR_60SPS 1
#define MCP3425_DR_15SPS 2
#define MCP3425_MODE_SINGLE_SHOT 0
#define MCP3425_MODE_CONTINUOUS 1

class A2D_4CH_Isolated_ADC
{
public:
    uint8_t address = 0;
    uint8_t gains[4] = {};
    uint8_t rates[4] = {};
    uint8_t modes[4] = {};
    bool io_ok = true, storage_ok = true;
    uint32_t next_reserved = 1;
    bool reserve_request_ids(uint32_t &first,uint32_t &last) { if(!storage_ok || !next_reserved)return false;first=next_reserved;last=first+65535;next_reserved=last+1;return true; }
    uint8_t failing_channel = 255;
    float voltage_scale = 1.0f, cal_gain = 1.0f, cal_offset = 0.0f;
    bool receive = true;
    bool led = false;
    unsigned int calibration_calls = 0;
    unsigned int reset_calls = 0, trigger_calls = 0, address_saves = 0;
    uint8_t calibration_channel = 255, reset_mask = 0, save_mask = 0;
    float calibration_points[4] = {};
    bool init() { return io_ok && storage_ok; }
    bool reset() { ++reset_calls; return io_ok && storage_ok; }
    bool force_eeprom_reinit() { return storage_ok; }
    bool trigger_all_single_shot() { ++trigger_calls; return io_ok; }
    void set_led(bool value) { led = value; }
    float measure_voltage(uint8_t ch) { return ch == failing_channel ? NAN : (ch + 1.0f) * voltage_scale; }
    float measure_raw_voltage(uint8_t ch) { return ch == failing_channel ? NAN : (ch + 1) * 0.1f; }
    void reset_all_calibration() { reset_mask = 15; }
    void reset_calibration(uint8_t ch) { reset_mask |= 1 << ch; }
    bool save_all_calibration() { save_mask = 15; return storage_ok; }
    bool save_calibration(uint8_t ch) { save_mask |= 1 << ch; return storage_ok; }
    bool try_calibrate_voltage(uint8_t ch, float a1, float m1, float a2, float m2) {
        ++calibration_calls; calibration_channel = ch;
        calibration_points[0] = a1; calibration_points[1] = m1;
        calibration_points[2] = a2; calibration_points[3] = m2;
        return true;
    }
    float get_cal_offset(uint8_t) { return cal_offset; }
    float get_cal_gain(uint8_t) { return cal_gain; }
    bool set_gain(uint8_t ch, uint8_t value) { gains[ch] = value; return io_ok; }
    bool set_data_rate(uint8_t ch, uint8_t value) { rates[ch] = value; return io_ok; }
    bool set_mode(uint8_t ch, uint8_t value) { modes[ch] = value; return io_ok; }
    void set_rs485_addr(uint8_t value) { address = value; }
    uint8_t get_rs485_addr() { return address; }
    bool save_rs485_addr() { ++address_saves; return storage_ok; }
    void set_rs485_receive(bool value) { receive = value; }
};

#endif
