#ifndef A2D_MAIN_H
#define A2D_MAIN_H
#include "stm32f1xx_hal.h"
#include "A2D_HAL.h"
#ifdef __cplusplus
extern "C" {
#endif
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart3;
extern PCD_HandleTypeDef hpcd;
void Error_Handler(void);
void A2D_usb_init(void);
int A2D_usb_available(void);
size_t A2D_usb_read(uint8_t *,size_t);
size_t A2D_usb_write(const uint8_t *,size_t);
int A2D_usb_write_space(void);
int A2D_uart_available(void);
void A2D_uart_start(void);
size_t A2D_uart_read(uint8_t *,size_t);
size_t A2D_uart_write(const uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
#endif
