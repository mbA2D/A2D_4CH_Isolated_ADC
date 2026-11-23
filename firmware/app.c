#include "A2D_Application.h"
#include "A2D_USB_Bootloader.h"
static unsigned usb_id = A2D_PORT_USB, rs485_id = A2D_PORT_RS485;
static int available(void *context) { return A2D_port_available(*(unsigned *)context); }
static size_t read_bytes(void *context, char *data, size_t size) { return A2D_port_read(*(unsigned *)context, (uint8_t *)data, size); }
static size_t write_bytes(void *context, const uint8_t *data, size_t size) { return A2D_port_write(*(unsigned *)context, data, size); }
static int write_space(void *context) { return A2D_port_write_space(*(unsigned *)context); }
static bool connected(void *context) { return A2D_port_connected(*(unsigned *)context); }
static void drain(void *context) { (void)context; } /* HAL UART write already waits for TC. */
static bool transmit(void *context, uint8_t address, uint8_t value) { (void)context; return A2D_i2c_write(address, value); }
static bool receive(void *context, uint8_t address, uint8_t *data, size_t size) { (void)context; return A2D_i2c_read(address, data, size); }
static A2D_I2c i2c = {NULL, transmit, receive};
static A2D_Port usb = {&usb_id, available, read_bytes, write_bytes, write_space, connected, drain};
static A2D_Port rs485 = {&rs485_id, available, read_bytes, write_bytes, write_space, connected, drain};
static A2D_Board board;
static A2D_Transport transport;
A2D_I2c *a2d_default_i2c_bus(void) { return &i2c; }
void A2D_app_init(void) {
    a2d_board_init(&board, NULL);
    a2d_transport_init(&transport, &usb, &rs485, &board);
}
void A2D_app_poll(void) { A2D_service_usb_bootloader_reset(); a2d_application_poll(&transport); }
