#include "A2D_Transport.h"
#include "A2D_USB_Session.h"
#include <string.h>
#ifdef ARDUINO_ARCH_STM32
#include <cdc_queue.h>
#include <usbd_cdc_if.h>
static_assert(CDC_TRANSMIT_QUEUE_BUFFER_SIZE >= 128, "USB TX queue must fit normal replies");
#endif
namespace a2d {
namespace {
// CDC session callbacks can purge the queues. Keep only the bounded queue
// read/enqueue operations atomic; waits and command execution stay outside.
struct UsbGuard {
#ifdef ARDUINO_ARCH_STM32
    uint32_t mask;
    UsbGuard() : mask(__get_PRIMASK()) { __disable_irq(); }
    ~UsbGuard() { __set_PRIMASK(mask); }
#else
    UsbGuard() {}
    ~UsbGuard() {}
#endif
};
bool usbConnected(USBSerial &usb) {
#ifdef ARDUINO_ARCH_STM32
    (void)usb; return CDC_connected(); // USBSerial::operator bool itself sleeps10ms.
#else
    return static_cast<bool>(usb);
#endif
}
size_t digits(uint32_t value) { size_t count = 1; while (value >= 10) { value /= 10; ++count; } return count; }
bool decimal(const char *&cursor, uint32_t maximum, uint32_t &value) {
    if (*cursor < '0' || *cursor > '9') return false;
    value = 0;
    do {
        const uint32_t digit = static_cast<uint32_t>(*cursor++ - '0');
        if (value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    } while (*cursor >= '0' && *cursor <= '9');
    return *cursor++ == ' ';
}
bool decode(const char *frame, uint8_t &address, uint32_t &id, const char *&payload) {
    uint32_t destination;
    if (!decimal(frame, 31, destination) || !decimal(frame, 0xffffffffUL, id) || !id || !*frame) return false;
    address = static_cast<uint8_t>(destination); payload = frame;
    return true;
}
}
Transport::Transport(USBSerial &usb, Stream &rs485, A2D_4CH_Isolated_ADC &board)
    : usb_(usb), rs485_(rs485), board_(board), waiting_(false), expectedAddress_(0), replyAddress_(0),
      started_(0), nextId_(0), lastId_(0), expectedId_(0), replyId_(0), usbSession_(0) {}
size_t Transport::ReplyBuffer::write(const uint8_t *bytes, size_t count) {
    if (count > sizeof(data) - size) { overflow = true; return 0; }
    memcpy(data + size, bytes, count); size += count; return count;
}
void Transport::reset() { usbLine_.reset(); rs485Line_.reset(); output_.clear(); waiting_ = false; }
void Transport::serviceUsbSession() {
    const UsbGuard guard;
    const uint32_t current = A2D_usb_session_generation();
    if (current == usbSession_) return;
    usbSession_ = current;
    usbLine_.reset();
    output_.clear();
    if (waiting_) rs485Line_.reset();
    waiting_ = false; // Do not deliver the previous USB client's forwarded reply.
    // Durable request IDs and all instrument settings deliberately survive.
}
bool Transport::hasBufferedInput(Source source) const {
    return source == Source::Usb ? usbLine_.has_pending_input() : rs485Line_.has_pending_input();
}
int Transport::poll(Source source, char *line) {
    if (source == Source::Usb) {
        const UsbGuard guard;
        serviceUsbSession();
        return A2D_usb_session_open() ? usbLine_.poll(usb_, line) : 0;
    }
    const int result = rs485Line_.poll(rs485_, line);
    if (result <= 0) return result;
    uint8_t address; uint32_t id; const char *payload;
    if (!decode(line, address, id, payload) || address != board_.get_rs485_addr()) return 0;
    replyAddress_ = address; replyId_ = id;
    memmove(line, payload, strlen(payload) + 1);
    return 1;
}
Print &Transport::beginReply(Source) { output_.clear(); return output_; }
void Transport::sendUsb(const uint8_t *data, size_t size) {
    const uint32_t start = static_cast<uint32_t>(millis());
    do {
        bool queued = false;
        // Only this thread enqueues USB output. IRQs can only increase free space.
        // Enqueue the complete reply in one write, so a timeout never leaves half a line.
        {
            const UsbGuard guard;
            if (A2D_usb_session_generation() != usbSession_ || !A2D_usb_session_open() || !usbConnected(usb_)) return;
            if (usb_.availableForWrite() >= static_cast<int>(size)) {
                if (usb_.write(data, size) != size) return;
                queued = true;
            }
        }
        if (queued) {
            // Preserve completed-reply pacing, with the same deadline and disconnect check.
            // This replaces flush without its unbounded wait or sleeping boolean operator.
            while (usb_.availableForWrite() < 127 &&
                   static_cast<uint32_t>(static_cast<uint32_t>(millis()) - start) < 100U) {
                if (A2D_usb_session_generation() != usbSession_ || !usbConnected(usb_)) return;
                delay(1);
            }
            return;
        }
        delay(1);
    } while (static_cast<uint32_t>(static_cast<uint32_t>(millis()) - start) < 100U);
}
void Transport::endReply(Source source) {
    static const char tooLong[] = "ERROR:RESPONSE TOO LONG\r\n";
    const uint8_t *data = output_.data; size_t size = output_.size;
    if (output_.overflow) { data = reinterpret_cast<const uint8_t *>(tooLong); size = sizeof(tooLong) - 1; }
    if (source == Source::Usb) {
        // Keep the core's proven default two-packet TX queue. Oversized replies
        // become a bounded error instead of requiring a larger USB queue.
        if (size > 127) { data = reinterpret_cast<const uint8_t *>(tooLong); size = sizeof(tooLong) - 1; }
        sendUsb(data, size); return;
    }
    const size_t prefix = digits(replyAddress_) + digits(replyId_) + 2;
    if (prefix + size > LineCapacity) return;
    board_.set_rs485_receive(false);
    rs485_.print(replyAddress_); rs485_.print(" "); rs485_.print(replyId_); rs485_.print(" ");
    rs485_.write(data, size);
    rs485_.flush(); // UART transmission must finish before releasing DE.
    board_.set_rs485_receive(true);
}
void Transport::reply(Source source, const char *line) { beginReply(source).println(line); endReply(source); }
void Transport::forward(uint8_t address, const char *line, bool expectsReply) {
    // Reserve a full 32-bit ID and CRLF, keeping capacity constant over the session.
    const size_t prefix = address < 10 ? 13 : 14;
    if (strlen(line) + prefix + 1 >= LineCapacity) { reply(Source::Usb, "ERROR:LINE TOO LONG"); return; }
    rs485Line_.reset();
    while (rs485_.available()) rs485_.read();
    if (nextId_ == lastId_) {
        uint32_t first, last;
        if (!board_.reserve_request_ids(first, last)) { reply(Source::Usb, "ERROR:STORAGE WRITE"); return; }
        nextId_ = first - 1; lastId_ = last;
    }
    ++nextId_;
    expectedAddress_ = address; expectedId_ = nextId_;
    output_.clear();
    output_.print(address); output_.print(" "); output_.print(expectedId_); output_.print(" "); output_.println(line);
    if (output_.overflow) { reply(Source::Usb, "ERROR:LINE TOO LONG"); return; }
    board_.set_rs485_receive(false);
    rs485_.write(output_.data, output_.size);
    rs485_.flush(); board_.set_rs485_receive(true);
    started_ = static_cast<uint32_t>(millis()); waiting_ = expectsReply;
}
void Transport::pollForward() {
    if (!waiting_) return;
    if (static_cast<uint32_t>(static_cast<uint32_t>(millis()) - started_) >= 1000U) {
        rs485Line_.reset(); waiting_ = false; reply(Source::Usb, "ERROR:RS485 TIMEOUT"); return;
    }
    char frame[LineCapacity]; const int result = rs485Line_.poll(rs485_, frame);
    if (!result) return;
    if (result < 0) return; // An overlong frame has no trustworthy transaction ID.
    uint8_t address; uint32_t id; const char *payload;
    if (!decode(frame, address, id, payload) || address != expectedAddress_ || id != expectedId_) return;
    waiting_ = false; reply(Source::Usb, payload);
}
}
