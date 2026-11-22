#ifndef A2D_TRANSPORT_H
#define A2D_TRANSPORT_H
#include <Arduino.h>
#include <A2D_4CH_Isolated_ADC.h>
#include <A2D_ScpiLineReader.h>
#include "A2D_Protocol.h"
namespace a2d {
class Transport {
public:
    Transport(USBSerial &usb, Stream &rs485, A2D_4CH_Isolated_ADC &board);
    int poll(Source source, char *line);
    Print &beginReply(Source source);
    void endReply(Source source);
    void reply(Source source, const char *line);
    void forward(uint8_t address, const char *line, bool expectsReply);
    void pollForward();
    bool busy() const { return waiting_; }
    bool hasBufferedInput(Source source) const;
    void reset();
    void serviceUsbSession();
private:
    class ReplyBuffer : public Print {
    public:
        ReplyBuffer() : size(0), overflow(false) {}
        size_t write(uint8_t byte) override { return write(&byte, 1); }
        size_t write(const uint8_t *data, size_t count) override;
        void clear() { size = 0; overflow = false; }
        uint8_t data[LineCapacity]; size_t size; bool overflow;
    } output_;
    void sendUsb(const uint8_t *data, size_t size);
    USBSerial &usb_;
    Stream &rs485_;
    A2D_4CH_Isolated_ADC &board_;
    a2d_scpi::LineReader<LineCapacity> usbLine_, rs485Line_;
    bool waiting_;
    uint8_t expectedAddress_, replyAddress_;
    uint32_t started_, nextId_, lastId_, expectedId_, replyId_;
    uint32_t usbSession_;
};
}
#endif
