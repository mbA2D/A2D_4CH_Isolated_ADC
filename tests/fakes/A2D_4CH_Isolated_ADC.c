#include "A2D_4CH_Isolated_ADC.h"
#include <math.h>
#include <string.h>

void a2d_fake_board_defaults(A2D_Board *board) {
    memset(board, 0, sizeof(*board));
    board->serial_value = 2;
    strcpy(board->serial_text, "0002");
    board->io_ok = board->storage_ok = board->receive = true;
    board->next_reserved = 1;
    board->failing_channel = board->calibration_channel = 255;
    board->voltage_scale = board->cal_gain = 1.0f;
}

bool a2d_board_init(A2D_Board *board, A2D_I2c *i2c) { (void)i2c; return board->io_ok && board->storage_ok; }
bool a2d_board_reset(A2D_Board *board) { ++board->reset_calls; return board->io_ok && board->storage_ok; }
float a2d_board_measure_voltage(A2D_Board *board, uint8_t channel) {
    return channel == board->failing_channel ? NAN : (channel + 1.0f) * board->voltage_scale;
}
float a2d_board_measure_raw_voltage(A2D_Board *board, uint8_t channel) {
    return channel == board->failing_channel ? NAN : (channel + 1) * 0.1f;
}
bool a2d_board_calibrate(A2D_Board *board, uint8_t channel, float actual1, float measured1, float actual2, float measured2) {
    ++board->calibration_calls;
    board->calibration_channel = channel;
    board->calibration_points[0] = actual1; board->calibration_points[1] = measured1;
    board->calibration_points[2] = actual2; board->calibration_points[3] = measured2;
    return true;
}
void a2d_board_reset_calibration(A2D_Board *board, uint8_t channel) { board->reset_mask |= 1 << channel; }
void a2d_board_reset_all_calibration(A2D_Board *board) { board->reset_mask = 15; }
bool a2d_board_save_calibration(A2D_Board *board, uint8_t channel) { board->save_mask |= 1 << channel; return board->storage_ok; }
bool a2d_board_save_all_calibration(A2D_Board *board) { board->save_mask = 15; return board->storage_ok; }
float a2d_board_cal_offset(const A2D_Board *board, uint8_t channel) { (void)channel; return board->cal_offset; }
float a2d_board_cal_gain(const A2D_Board *board, uint8_t channel) { (void)channel; return board->cal_gain; }
bool a2d_board_set_gain(A2D_Board *board, uint8_t channel, uint8_t gain) { board->gains[channel] = gain; return board->io_ok; }
bool a2d_board_set_data_rate(A2D_Board *board, uint8_t channel, uint8_t rate) { board->rates[channel] = rate; return board->io_ok; }
bool a2d_board_set_mode(A2D_Board *board, uint8_t channel, uint8_t mode) { board->modes[channel] = mode; return board->io_ok; }
bool a2d_board_trigger(A2D_Board *board) { ++board->trigger_calls; return board->io_ok; }
void a2d_board_set_rs485_receive(A2D_Board *board, bool receive) { board->receive = receive; }
void a2d_board_set_address(A2D_Board *board, uint8_t address) { board->address = address; }
bool a2d_board_save_address(A2D_Board *board) { ++board->address_saves; return board->storage_ok; }
bool a2d_board_reserve_request_ids(A2D_Board *board, uint32_t *first, uint32_t *last) {
    if (!board->storage_ok || !board->next_reserved) return false;
    *first = board->next_reserved; *last = *first + 65535; board->next_reserved = *last + 1;
    return true;
}
uint8_t a2d_board_address(const A2D_Board *board) { return board->address; }
void a2d_board_set_led(A2D_Board *board, bool on) { board->led = on; }
const char *a2d_board_serial(A2D_Board *board) { return board->serial_text; }
uint32_t a2d_board_serial_number(const A2D_Board *board) { return board->serial_value; }
bool a2d_board_provision_serial(A2D_Board *board, uint16_t serial) {
    if (!board->storage_ok || (board->serial_value != 0xffffffffUL && board->serial_value != serial)) return false;
    board->serial_value = serial; board->serial_text[4] = 0;
    for (int i = 3; i >= 0; --i) { board->serial_text[i] = (char)('0' + serial % 10); serial /= 10; }
    return true;
}
bool a2d_board_initialize_settings(A2D_Board *board) { return board->storage_ok; }

bool a2d_board_get_configuration(A2D_Board *board, uint8_t channel, uint8_t *gain, uint8_t *rate, uint8_t *mode) {
    static const uint8_t rates[] = {240, 60, 15};
    if (!board->io_ok || channel >= 4 || channel == board->failing_channel ||
        board->gains[channel] > 3 || board->rates[channel] > 2 || board->modes[channel] > 1) return false;
    *gain = (uint8_t)(1U << board->gains[channel]); *rate = rates[board->rates[channel]]; *mode = board->modes[channel];
    return true;
}
