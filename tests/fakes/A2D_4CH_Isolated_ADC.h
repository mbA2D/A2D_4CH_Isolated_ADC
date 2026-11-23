#ifndef TEST_A2D_ADC_H
#define TEST_A2D_ADC_H

#include "A2D_Platform.h"
#include "A2D_BoardConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fake state is shared by the C production objects and C++ assertions. */
typedef struct A2D_Board {
    uint32_t serial_value;
    char serial_text[11];
    uint8_t address, gains[4], rates[4], modes[4];
    bool io_ok, storage_ok;
    uint32_t next_reserved;
    uint8_t failing_channel;
    float voltage_scale, cal_gain, cal_offset;
    bool receive, led;
    unsigned int calibration_calls, reset_calls, trigger_calls, address_saves;
    uint8_t calibration_channel, reset_mask, save_mask;
    float calibration_points[4];
} A2D_Board;

/* Fixture defaults; board_init itself retains configured fake state. */
void a2d_fake_board_defaults(A2D_Board *board);

/* Keep these signatures identical to the production board C header. */
bool a2d_board_init(A2D_Board *board, A2D_I2c *i2c);
bool a2d_board_reset(A2D_Board *board);
float a2d_board_measure_voltage(A2D_Board *board, uint8_t channel);
float a2d_board_measure_raw_voltage(A2D_Board *board, uint8_t channel);
bool a2d_board_calibrate(A2D_Board *board, uint8_t channel, float actual1, float measured1, float actual2, float measured2);
void a2d_board_reset_calibration(A2D_Board *board, uint8_t channel);
void a2d_board_reset_all_calibration(A2D_Board *board);
bool a2d_board_save_calibration(A2D_Board *board, uint8_t channel);
bool a2d_board_save_all_calibration(A2D_Board *board);
float a2d_board_cal_offset(const A2D_Board *board, uint8_t channel);
float a2d_board_cal_gain(const A2D_Board *board, uint8_t channel);
bool a2d_board_set_gain(A2D_Board *board, uint8_t channel, uint8_t gain);
bool a2d_board_set_data_rate(A2D_Board *board, uint8_t channel, uint8_t rate);
bool a2d_board_set_mode(A2D_Board *board, uint8_t channel, uint8_t mode);
bool a2d_board_trigger(A2D_Board *board);
bool a2d_board_get_configuration(A2D_Board *board, uint8_t channel, uint8_t *gain, uint8_t *rate, uint8_t *mode);
void a2d_board_set_rs485_receive(A2D_Board *board, bool receive);
void a2d_board_set_address(A2D_Board *board, uint8_t address);
bool a2d_board_save_address(A2D_Board *board);
bool a2d_board_reserve_request_ids(A2D_Board *board, uint32_t *first, uint32_t *last);
uint8_t a2d_board_address(const A2D_Board *board);
void a2d_board_set_led(A2D_Board *board, bool on);
const char *a2d_board_serial(A2D_Board *board);
uint32_t a2d_board_serial_number(const A2D_Board *board);
bool a2d_board_provision_serial(A2D_Board *board, uint16_t serial);
bool a2d_board_initialize_settings(A2D_Board *board);

#ifdef __cplusplus
}
#endif
#endif
