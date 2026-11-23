#ifndef A2D_TRANSPORT_H
#define A2D_TRANSPORT_H
#include "A2D_Platform.h"
#include <A2D_4CH_Isolated_ADC.h>
#include <A2D_ScpiLineReader.h>
#include <A2D_ScpiErrorQueue.h>
#include "A2D_Protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t data[A2D_LINE_CAPACITY]; size_t size; bool overflow;
} A2D_ReplyBuffer;
typedef struct {
    A2D_Port *usb, *rs485; A2D_Board *board;
    A2D_ReplyBuffer output;
    A2D_ScpiErrorQueue errors; /* Instrument-wide; survives *RST and USB sessions. */
    char usb_storage[A2D_LINE_CAPACITY], rs485_storage[A2D_LINE_CAPACITY];
    A2D_ScpiLineReader usb_line, rs485_line;
    uint8_t usb_pending[127]; size_t usb_pending_size;
    bool waiting, prefer_rs485; uint8_t expected_address, reply_address;
    uint32_t started, next_id, last_id, expected_id, reply_id, usb_session;
} A2D_Transport;
/* Context owns its buffers; initialize in its final location, do not copy/move. */
void a2d_transport_init(A2D_Transport *transport, A2D_Port *usb, A2D_Port *rs485, A2D_Board *board);
int a2d_transport_poll(A2D_Transport *transport, A2D_Source source, char *line);
A2D_Writer a2d_transport_begin_reply(A2D_Transport *transport, A2D_Source source);
bool a2d_transport_end_reply(A2D_Transport *transport, A2D_Source source);
bool a2d_transport_reply(A2D_Transport *transport, A2D_Source source, const char *line);
void a2d_transport_error(A2D_Transport *transport, int16_t code);
void a2d_transport_forward(A2D_Transport *transport, uint8_t address, const char *line, bool expects_reply);
void a2d_transport_poll_forward(A2D_Transport *transport);
bool a2d_transport_has_buffered_input(const A2D_Transport *transport, A2D_Source source);
void a2d_transport_reset(A2D_Transport *transport);
void a2d_transport_service_usb_session(A2D_Transport *transport);
/* Try one atomic enqueue; retain a complete reply while the host is not reading. */
void a2d_transport_service_usb_output(A2D_Transport *transport);

#ifdef __cplusplus
}
#endif
#endif
