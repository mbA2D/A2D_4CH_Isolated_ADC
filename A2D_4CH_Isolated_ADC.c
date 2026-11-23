#include "A2D_4CH_Isolated_ADC.h"
#include <string.h>
bool a2d_board_init(A2D_Board *board, A2D_I2c *i2c) {
    a2d_calibration_init(&board->calibration);
    a2d_settings_init(&board->storage);
    board->address = 0;
    a2d_board_set_led(board, false);
    a2d_board_set_rs485_receive(board, true);
    const bool adc_ok = a2d_measurement_begin(&board->measurement, i2c);
    const bool storage_ok = a2d_settings_load(&board->storage, &board->calibration, &board->address);
    return adc_ok && storage_ok;
}
bool a2d_board_reset(A2D_Board *board) {
    a2d_board_set_led(board, false);
    a2d_board_set_rs485_receive(board, true);
    const bool adc_ok = a2d_measurement_reset(&board->measurement);
    const bool storage_ok = a2d_settings_load(&board->storage, &board->calibration, &board->address);
    return adc_ok && storage_ok;
}
float a2d_board_measure_raw_voltage(A2D_Board *board, uint8_t channel) { return a2d_measurement_read_raw(&board->measurement, channel); }
float a2d_board_measure_voltage(A2D_Board *board, uint8_t channel) { return a2d_calibration_apply(&board->calibration, channel, a2d_measurement_read_raw(&board->measurement, channel)); }
bool a2d_board_calibrate(A2D_Board *board, uint8_t channel, float a1, float m1, float a2, float m2) { return a2d_calibration_calibrate(&board->calibration, channel, a1, m1, a2, m2); }
void a2d_board_reset_calibration(A2D_Board *board, uint8_t channel) { a2d_calibration_reset(&board->calibration, channel); }
void a2d_board_reset_all_calibration(A2D_Board *board) { a2d_calibration_reset_all(&board->calibration); }
bool a2d_board_save_calibration(A2D_Board *board, uint8_t channel) { return a2d_settings_save_calibration(&board->storage, &board->calibration, channel); }
bool a2d_board_save_all_calibration(A2D_Board *board) { return a2d_settings_save_all_calibration(&board->storage, &board->calibration); }
float a2d_board_cal_offset(const A2D_Board *board, uint8_t channel) { return a2d_calibration_get(&board->calibration, channel).offset; }
float a2d_board_cal_gain(const A2D_Board *board, uint8_t channel) { return a2d_calibration_get(&board->calibration, channel).gain; }
bool a2d_board_set_gain(A2D_Board *board, uint8_t channel, uint8_t gain) { return a2d_measurement_set_gain(&board->measurement, channel, gain); }
bool a2d_board_set_data_rate(A2D_Board *board, uint8_t channel, uint8_t rate) { return a2d_measurement_set_rate(&board->measurement, channel, rate); }
bool a2d_board_set_mode(A2D_Board *board, uint8_t channel, uint8_t mode) { return a2d_measurement_set_mode(&board->measurement, channel, mode); }
bool a2d_board_trigger(A2D_Board *board) { return a2d_measurement_trigger(&board->measurement); }
void a2d_board_set_rs485_receive(A2D_Board *board, bool receive) { (void)board; A2D_set_rs485_receive(receive); }
void a2d_board_set_address(A2D_Board *board, uint8_t address) { board->address = address; }
bool a2d_board_save_address(A2D_Board *board) { return a2d_settings_save_address(&board->storage, board->address); }
uint8_t a2d_board_address(const A2D_Board *board) { return board->address; }
void a2d_board_set_led(A2D_Board *board, bool on) { (void)board; A2D_set_led(on); }
uint32_t a2d_board_serial_number(const A2D_Board *board) { return a2d_settings_serial(&board->storage); }
const char *a2d_board_serial(A2D_Board *board) {
    uint32_t number = a2d_settings_serial(&board->storage);
    if (number == A2D_UNASSIGNED_SERIAL) { strcpy(board->serial, "UNASSIGNED"); return board->serial; }
    board->serial[4] = '\0';
    for (int i = 3; i >= 0; --i) { board->serial[i] = (char)('0' + number % 10); number /= 10; }
    return board->serial;
}
bool a2d_board_provision_serial(A2D_Board *board, uint16_t serial) { return a2d_settings_provision_serial(&board->storage, serial); }
bool a2d_board_initialize_settings(A2D_Board *board) { return a2d_settings_initialize(&board->storage, &board->calibration, &board->address) && a2d_board_reset(board); }
bool a2d_board_reserve_request_ids(A2D_Board *board, uint32_t *first, uint32_t *last) { return a2d_settings_reserve_request_ids(&board->storage, first, last); }

bool a2d_board_get_configuration(A2D_Board *board, uint8_t channel, uint8_t *gain, uint8_t *rate, uint8_t *mode) {
    static const uint8_t rates[] = {240, 60, 15};
    uint8_t config;
    if (!a2d_measurement_get_configuration(&board->measurement, channel, &config)) return false;
    *gain = (uint8_t)(1U << (config & 3));
    *rate = rates[(config >> 2) & 3];
    *mode = (config >> 4) & 1;
    return true;
}
