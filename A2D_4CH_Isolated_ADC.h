#ifndef A2D_4CH_ISOLATED_ADC_H
#define A2D_4CH_ISOLATED_ADC_H
#include "A2D_Platform.h"
#include "A2D_BoardConfig.h"
#include "A2D_Calibration.h"
#include "A2D_SettingsStore.h"
#include "A2D_Measurement.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct A2D_Board {
    A2D_Measurement measurement;
    A2D_Calibration calibration;
    A2D_SettingsStore storage;
    uint8_t address;
    char serial[11];
} A2D_Board;
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
