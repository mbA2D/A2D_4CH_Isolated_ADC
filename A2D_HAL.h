#ifndef A2D_HAL_H
#define A2D_HAL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { A2D_PORT_USB, A2D_PORT_RS485 };
uint32_t A2D_millis(void);
void A2D_delay(uint32_t ms);
uint32_t A2D_irq_lock(void);
void A2D_irq_unlock(uint32_t mask);
/* Mask only USB; UART/DMA must remain serviceable during USB parsing. */
uint32_t A2D_usb_irq_lock(void);
void A2D_usb_irq_unlock(uint32_t enabled);
void A2D_set_led(bool on);
void A2D_set_rs485_receive(bool receive);
bool A2D_i2c_write(uint8_t address, uint8_t value);
bool A2D_i2c_read(uint8_t address, uint8_t *data, size_t size);
int A2D_port_available(unsigned port);
size_t A2D_port_read(unsigned port, uint8_t *data, size_t size);
size_t A2D_port_write(unsigned port, const uint8_t *data, size_t size);
int A2D_port_write_space(unsigned port);
bool A2D_port_connected(unsigned port);
void A2D_enter_bootloader(void);
uint32_t A2D_usb_session_generation(void);
bool A2D_usb_session_open(void);
void A2D_app_init(void);
void A2D_app_poll(void);
#ifdef __cplusplus
}
#endif
#endif
