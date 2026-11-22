#ifndef A2D_SCPI_LINE_READER_H
#define A2D_SCPI_LINE_READER_H

#include <stddef.h>
#include <string.h>

namespace a2d_scpi {

// Keep one instance per transport. Capacity includes the terminating NUL.
template <size_t Capacity>
class LineBuffer {
    static_assert(Capacity > 1, "A line buffer needs room for text and NUL");
public:
    enum Result { Pending, Complete, TooLong };
    LineBuffer() { reset(); }
    void reset() { length_ = 0; overflow_ = false; data_[0] = '\0'; }

    Result push(char value) {
        if (value == '\n') {
            if (overflow_) { reset(); return TooLong; }
            if (length_ && data_[length_ - 1] == '\r') --length_;
            data_[length_] = '\0';
            return Complete;
        }
        if (!overflow_) {
            // Discard embedded NULs and overlong frames through the next LF.
            if (value == '\0' || length_ == Capacity - 1) overflow_ = true;
            else data_[length_++] = value;
        }
        return Pending;
    }
    const char *data() const { return data_; }
private:
    char data_[Capacity];
    size_t length_;
    bool overflow_;
};

// Port supplies available() and readBytes(char*, size_t), returning <= requested
// bytes. Both must be nonblocking for bytes already reported available. There is
// no Arduino dependency. Alternatively feed LineBuffer directly from any driver.
template <size_t Capacity>
class LineReader {
public:
    LineReader() { reset(); }
    void reset() { line_.reset(); position_ = count_ = 0; }
    bool has_pending_input() const { return position_ < count_; }

    // output must have Capacity bytes. Returns 1=complete, 0=pending, -1=invalid.
    // Consume at most 64 bytes per call, retaining coalesced/fragmented input.
    template <typename Port>
    int poll(Port &port, char *output) {
        for (size_t consumed = 0; consumed < sizeof(pending_); ++consumed) {
            if (position_ == count_) {
                const int available = port.available();
                if (available <= 0) return 0;
                const size_t wanted = static_cast<size_t>(available) < sizeof(pending_) ?
                    static_cast<size_t>(available) : sizeof(pending_);
                count_ = port.readBytes(pending_, wanted);
                position_ = 0;
                if (!count_) return 0;
            }
            const auto result = line_.push(pending_[position_++]);
            if (result == LineBuffer<Capacity>::TooLong) return -1;
            if (result == LineBuffer<Capacity>::Complete) {
                strcpy(output, line_.data());
                line_.reset();
                return 1;
            }
        }
        return 0;
    }
private:
    LineBuffer<Capacity> line_;
    char pending_[64];
    size_t position_, count_;
};

}
#endif
