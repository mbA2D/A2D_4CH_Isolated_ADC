#include "A2D_Application.h"
namespace a2d {
void Application::poll() {
    transport_.serviceUsbSession();
    // Keep reply order while waiting, without blocking the main loop.
    if (transport_.busy()) { transport_.pollForward(); return; }
    char line[LineCapacity];
    Source source = Source::Usb;
    int result = transport_.poll(source, line);
    if (!result) { source = Source::Rs485; result = transport_.poll(source, line); }
    if (!result) return;
    if (result < 0) {
        if (source == Source::Usb) transport_.reply(source, "ERROR:LINE TOO LONG");
        return;
    }
    Request request;
    switch (parse(line, source, board_.get_rs485_addr(), request)) {
    case ParseResult::Ready: dispatcher_.execute(request); break;
    case ParseResult::InvalidCommand: transport_.reply(source, "ERROR:INVALID COMMAND"); break;
    case ParseResult::InvalidArguments: transport_.reply(source, "ERROR:INVALID ARGUMENTS"); break;
    case ParseResult::Ignore: break;
    }
}
}
