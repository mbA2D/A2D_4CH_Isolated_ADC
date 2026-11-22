#include "A2D_4CH_Isolated_ADC.h"
#include <string.h>
A2D_4CH_Isolated_ADC::A2D_4CH_Isolated_ADC() : address_(0) {
    strcpy(serial_, A2D_4CH_ISO_ADC_DEFAULT_SERIAL_NUM);
}
bool A2D_4CH_Isolated_ADC::init(TwoWire *i2c) {
    pinMode(A2D_4CH_ISO_ADC_LED_PIN, OUTPUT);
    pinMode(A2D_4CH_ISO_ADC_RS485_DE_PIN, OUTPUT);
    set_led(false);
    set_rs485_receive(true);
    const bool adcOk = measurement_.begin(i2c);
    const bool storageOk = storage_.load(calibration_, address_);
    return adcOk && storageOk;
}
bool A2D_4CH_Isolated_ADC::reset() {
    set_led(false);
    set_rs485_receive(true);
    const bool adcOk = measurement_.reset();
    const bool storageOk = storage_.load(calibration_, address_);
    return adcOk && storageOk;
}
float A2D_4CH_Isolated_ADC::measure_raw_voltage(uint8_t ch) { return measurement_.readRaw(ch); }
float A2D_4CH_Isolated_ADC::measure_voltage(uint8_t ch) { return calibration_.apply(ch, measurement_.readRaw(ch)); }
bool A2D_4CH_Isolated_ADC::try_calibrate_voltage(uint8_t ch, float a1, float m1, float a2, float m2) {
    return calibration_.calibrate(ch, a1, m1, a2, m2);
}
void A2D_4CH_Isolated_ADC::calibrate_voltage(uint8_t ch, float a1, float m1, float a2, float m2) {
    try_calibrate_voltage(ch, a1, m1, a2, m2);
}
void A2D_4CH_Isolated_ADC::reset_calibration(uint8_t ch) { calibration_.reset(ch); }
void A2D_4CH_Isolated_ADC::reset_all_calibration() { calibration_.resetAll(); }
bool A2D_4CH_Isolated_ADC::save_calibration(uint8_t ch) { return storage_.saveCalibration(calibration_, ch); }
bool A2D_4CH_Isolated_ADC::save_all_calibration() { return storage_.saveAllCalibration(calibration_); }
float A2D_4CH_Isolated_ADC::get_cal_offset(uint8_t ch) { return calibration_.get(ch).offset; }
float A2D_4CH_Isolated_ADC::get_cal_gain(uint8_t ch) { return calibration_.get(ch).gain; }
bool A2D_4CH_Isolated_ADC::set_gain(uint8_t ch, uint8_t gain) { return measurement_.setGain(ch, gain); }
bool A2D_4CH_Isolated_ADC::set_data_rate(uint8_t ch, uint8_t rate) { return measurement_.setRate(ch, rate); }
bool A2D_4CH_Isolated_ADC::set_mode(uint8_t ch, uint8_t mode) { return measurement_.setMode(ch, mode); }
bool A2D_4CH_Isolated_ADC::trigger_all_single_shot() { return measurement_.trigger(); }
void A2D_4CH_Isolated_ADC::set_rs485_receive(bool state) { digitalWrite(A2D_4CH_ISO_ADC_RS485_DE_PIN, !state); }
void A2D_4CH_Isolated_ADC::set_rs485_addr(uint8_t address) { address_ = address; }
bool A2D_4CH_Isolated_ADC::save_rs485_addr() { return storage_.saveAddress(address_); }
uint8_t A2D_4CH_Isolated_ADC::get_rs485_addr() { return address_; }
void A2D_4CH_Isolated_ADC::set_led(bool state) {
    digitalWrite(A2D_4CH_ISO_ADC_LED_PIN, state ? A2D_4CH_ISO_ADC_LED_ON : A2D_4CH_ISO_ADC_LED_OFF);
}
char *A2D_4CH_Isolated_ADC::get_serial_num() { return serial_; }
bool A2D_4CH_Isolated_ADC::force_eeprom_reinit() { return storage_.initialize(calibration_, address_) && reset(); }

bool A2D_4CH_Isolated_ADC::reserve_request_ids(uint32_t &first, uint32_t &last) { return storage_.reserveRequestIds(first, last); }
