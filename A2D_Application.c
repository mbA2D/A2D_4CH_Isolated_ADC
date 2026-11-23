#include "A2D_Application.h"
void a2d_application_poll(A2D_Transport *transport) {
    a2d_transport_service_usb_session(transport);
    a2d_transport_service_usb_output(transport);
    if (transport->waiting) { a2d_transport_poll_forward(transport); return; }
    char line[A2D_LINE_CAPACITY];
    A2D_Source source = transport->prefer_rs485 ? A2D_SOURCE_RS485 : A2D_SOURCE_USB;
    int result = (source == A2D_SOURCE_USB && transport->usb_pending_size) ? 0 : a2d_transport_poll(transport, source, line);
    if (!result) {
        source = source == A2D_SOURCE_USB ? A2D_SOURCE_RS485 : A2D_SOURCE_USB;
        if (source != A2D_SOURCE_USB || !transport->usb_pending_size) result = a2d_transport_poll(transport, source, line);
    }
    if (!result) return;
    transport->prefer_rs485 = source == A2D_SOURCE_USB;
    if (result < 0) {
        if (source == A2D_SOURCE_USB) a2d_transport_error(transport, A2D_ERROR_INPUT_OVERRUN);
        return;
    }
    A2D_Request request;
    const A2D_ParseResult parsed = a2d_parse(line, source, a2d_board_address(transport->board), &request);
    switch (parsed) {
    case A2D_PARSE_READY: a2d_dispatch_execute(transport, &request); break;
    case A2D_PARSE_IGNORE: break;
    default: a2d_transport_error(transport, (int16_t)parsed); break;
    }
}
