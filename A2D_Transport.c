#include "A2D_Transport.h"
#include "A2D_USB_Session.h"
#include <string.h>
static size_t digits(uint32_t value) {
    size_t count = 1; while (value >= 10) { value /= 10; ++count; } return count;
}
static bool decimal(const char **cursor, uint32_t maximum, uint32_t *value) {
    const char *text = *cursor;
    if (*text < '0' || *text > '9') return false;
    uint32_t parsed = 0;
    do {
        const uint32_t digit = (uint32_t)(*text++ - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    } while (*text >= '0' && *text <= '9');
    if (*text != ' ') return false;
    *cursor = text + 1; *value = parsed; return true;
}
static bool decode(const char *frame, uint8_t *address, uint32_t *id, const char **payload) {
    uint32_t destination;
    if (!decimal(&frame, 31, &destination) || !decimal(&frame, UINT32_MAX, id) || !*id || !*frame) return false;
    *address = (uint8_t)destination; *payload = frame; return true;
}
static void clear_output(A2D_Transport *transport) { transport->output.size = 0; transport->output.overflow = false; }
static size_t buffer_write(void *context, const uint8_t *bytes, size_t count) {
    A2D_ReplyBuffer *output = context;
    if (count > sizeof(output->data) - output->size) { output->overflow = true; return 0; }
    memcpy(output->data + output->size, bytes, count); output->size += count; return count;
}
static int poll_port(A2D_ScpiLineReader *reader, A2D_Port *port, char *line) {
    const A2D_ScpiInput input = {port->context, port->available, port->read};
    return a2d_scpi_reader_poll(reader, &input, line);
}
void a2d_transport_init(A2D_Transport *transport, A2D_Port *usb, A2D_Port *rs485, A2D_Board *board) {
    memset(transport, 0, sizeof(*transport));
    transport->usb = usb; transport->rs485 = rs485; transport->board = board;
    a2d_scpi_reader_init(&transport->usb_line, transport->usb_storage, sizeof(transport->usb_storage));
    a2d_scpi_reader_init(&transport->rs485_line, transport->rs485_storage, sizeof(transport->rs485_storage));
}
void a2d_transport_reset(A2D_Transport *transport) {
    a2d_scpi_reader_reset(&transport->usb_line); a2d_scpi_reader_reset(&transport->rs485_line);
    clear_output(transport); transport->waiting = false; transport->usb_pending_size = 0;
}
void a2d_transport_service_usb_session(A2D_Transport *transport) {
    const uint32_t mask = A2D_usb_irq_lock();
    const uint32_t current = A2D_usb_session_generation();
    if (current != transport->usb_session) {
        transport->usb_session = current;
        a2d_scpi_reader_reset(&transport->usb_line); clear_output(transport); transport->usb_pending_size = 0;
        if (transport->waiting) a2d_scpi_reader_reset(&transport->rs485_line);
        transport->waiting = false; /* Preserve durable IDs and instrument settings. */
    }
    A2D_usb_irq_unlock(mask);
}
bool a2d_transport_has_buffered_input(const A2D_Transport *transport, A2D_Source source) {
    return a2d_scpi_reader_has_pending(source == A2D_SOURCE_USB ? &transport->usb_line : &transport->rs485_line);
}
int a2d_transport_poll(A2D_Transport *transport, A2D_Source source, char *line) {
    if (source == A2D_SOURCE_USB) {
        const uint32_t mask = A2D_usb_irq_lock();
        a2d_transport_service_usb_session(transport);
        const int result = A2D_usb_session_open() ? poll_port(&transport->usb_line, transport->usb, line) : 0;
        A2D_usb_irq_unlock(mask); return result;
    }
    const int result = poll_port(&transport->rs485_line, transport->rs485, line);
    if (result <= 0) return result;
    uint8_t address; uint32_t id; const char *payload;
    if (!decode(line, &address, &id, &payload) || address != a2d_board_address(transport->board)) return 0;
    transport->reply_address = address; transport->reply_id = id;
    memmove(line, payload, strlen(payload) + 1); return 1;
}
A2D_Writer a2d_transport_begin_reply(A2D_Transport *transport, A2D_Source source) {
    (void)source; clear_output(transport);
    return (A2D_Writer){&transport->output, buffer_write};
}
void a2d_transport_service_usb_output(A2D_Transport *transport) {
    if (!transport->usb_pending_size) return;
    A2D_Port *usb = transport->usb;
    const uint32_t mask = A2D_usb_irq_lock();
    if (A2D_usb_session_generation() != transport->usb_session || !A2D_usb_session_open() || !usb->connected(usb->context)) {
        transport->usb_pending_size = 0;
    } else if (usb->write_space(usb->context) >= (int)transport->usb_pending_size &&
               usb->write(usb->context, transport->usb_pending, transport->usb_pending_size) == transport->usb_pending_size) {
        transport->usb_pending_size = 0;
    }
    A2D_usb_irq_unlock(mask);
}
bool a2d_transport_end_reply(A2D_Transport *transport, A2D_Source source) {
    const uint8_t *data = transport->output.data; size_t size = transport->output.size;
    if (transport->output.overflow) { a2d_transport_error(transport, A2D_ERROR_QUERY); return false; }
    if (source == A2D_SOURCE_USB) {
        if (size > 127) { a2d_transport_error(transport, A2D_ERROR_QUERY); return false; }
        /* Application pauses USB commands while this one complete reply is pending.
           A separate buffer lets addressed RS485 requests continue meanwhile. */
        if (transport->usb_pending_size) return false;
        memcpy(transport->usb_pending, data, size); transport->usb_pending_size = size;
        a2d_transport_service_usb_output(transport); return true;
    }
    const size_t prefix = digits(transport->reply_address) + digits(transport->reply_id) + 2;
    if (prefix + size > A2D_LINE_CAPACITY) { a2d_transport_error(transport, A2D_ERROR_QUERY); return false; }
    A2D_ReplyBuffer frame; frame.size = 0; frame.overflow = false;
    A2D_Writer out = {&frame, buffer_write};
    a2d_writer_uint(&out, transport->reply_address); a2d_writer_text(&out, " ");
    a2d_writer_uint(&out, transport->reply_id); a2d_writer_text(&out, " ");
    buffer_write(&frame, data, size);
    A2D_Port *port = transport->rs485;
    a2d_board_set_rs485_receive(transport->board, false);
    const bool sent = !frame.overflow && port->write(port->context, frame.data, frame.size) == frame.size;
    port->drain(port->context); a2d_board_set_rs485_receive(transport->board, true);
    if (!sent) a2d_transport_error(transport, A2D_ERROR_RS485_WRITE);
    return sent;
}
bool a2d_transport_reply(A2D_Transport *transport, A2D_Source source, const char *line) {
    A2D_Writer out = a2d_transport_begin_reply(transport, source);
    a2d_writer_text(&out, line); a2d_writer_newline(&out); return a2d_transport_end_reply(transport, source);
}
void a2d_transport_forward(A2D_Transport *transport, uint8_t address, const char *line, bool expects_reply) {
    const size_t prefix = address < 10 ? 13 : 14;
    if (strlen(line) + prefix + 1 >= A2D_LINE_CAPACITY) { a2d_transport_error(transport, A2D_ERROR_INPUT_OVERRUN); return; }
    a2d_scpi_reader_reset(&transport->rs485_line);
    A2D_Port *port = transport->rs485;
    char discard;
    for (size_t drained = 0; port->available(port->context) > 0; ++drained) {
        if (drained == 512 || port->read(port->context, &discard, 1) != 1) {
            a2d_transport_error(transport, A2D_ERROR_RS485_BUSY); return;
        }
    }
    if (transport->next_id == transport->last_id) {
        uint32_t first, last;
        if (!a2d_board_reserve_request_ids(transport->board, &first, &last)) { a2d_transport_error(transport, A2D_ERROR_STORAGE); return; }
        transport->next_id = first - 1; transport->last_id = last;
    }
    ++transport->next_id;
    transport->expected_address = address; transport->expected_id = transport->next_id;
    A2D_Writer out = a2d_transport_begin_reply(transport, A2D_SOURCE_USB);
    a2d_writer_uint(&out, address); a2d_writer_text(&out, " ");
    a2d_writer_uint(&out, transport->expected_id); a2d_writer_text(&out, " ");
    a2d_writer_text(&out, line); a2d_writer_newline(&out);
    if (transport->output.overflow) { a2d_transport_error(transport, A2D_ERROR_INPUT_OVERRUN); return; }
    a2d_board_set_rs485_receive(transport->board, false);
    const bool sent = port->write(port->context, transport->output.data, transport->output.size) == transport->output.size;
    port->drain(port->context); a2d_board_set_rs485_receive(transport->board, true);
    if (!sent) { a2d_transport_error(transport, A2D_ERROR_RS485_WRITE); return; }
    transport->started = A2D_millis(); transport->waiting = expects_reply;
}
void a2d_transport_poll_forward(A2D_Transport *transport) {
    if (!transport->waiting) return;
    if ((uint32_t)(A2D_millis() - transport->started) >= 1000U) {
        a2d_scpi_reader_reset(&transport->rs485_line); transport->waiting = false;
        a2d_transport_error(transport, A2D_ERROR_RS485_TIMEOUT); return;
    }
    char frame[A2D_LINE_CAPACITY];
    const int result = poll_port(&transport->rs485_line, transport->rs485, frame);
    if (result <= 0) return;
    uint8_t address; uint32_t id; const char *payload;
    if (!decode(frame, &address, &id, &payload) || address != transport->expected_address || id != transport->expected_id) return;
    transport->waiting = false; a2d_transport_reply(transport, A2D_SOURCE_USB, payload);
}

void a2d_transport_error(A2D_Transport *transport, int16_t code) {
    a2d_scpi_errors_push(&transport->errors, code);
}
