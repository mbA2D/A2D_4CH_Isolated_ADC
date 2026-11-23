#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#define F(text) text
#define PB11 11
#define PB10 10
extern unsigned long test_clock;
inline unsigned long millis() { return test_clock++; }
inline void delay(unsigned long ms) { test_clock += ms; }
#include "A2D_Platform.h"
// C++ test storage with the callbacks used by the production C transport.
class Stream {
public:
    A2D_Port port;
    Stream() : port{this, portAvailable, portRead, portWrite, portWriteSpace, portConnected, portDrain} {}
    std::string input, output;
    std::vector<std::string> write_requests;
    std::function<void()> on_write, on_flush;
    bool connected = true, reject_write = false;
    bool endlessly_readable = false, reject_read = false;
    size_t read_bytes = 0, read_calls = 0, read_budget = static_cast<size_t>(-1);
    int space = 127, spaceAfterWrite = -1, write_limit = -1;
    bool isConnected() const { return connected; }
    void begin(unsigned long) {}
    int available() { return endlessly_readable ? 64 : static_cast<int>(input.size()); }
    int availableForWrite() { return space; }
    int read() { char value;return readBytes(&value,1)==1?static_cast<unsigned char>(value):-1; }
    size_t readBytes(char *data, size_t size) {
        ++read_calls;if(reject_read)return 0;
        const size_t count=endlessly_readable?size:(size<input.size()?size:input.size());
        if(read_bytes>read_budget || count>read_budget-read_bytes)throw std::runtime_error("fake read budget exceeded");
        if(endlessly_readable)memset(data,'X',count);else{memcpy(data,input.data(),count);input.erase(0,count);}
        read_bytes+=count;return count;
    }
    size_t write(uint8_t value) { return write(&value,1); }
    size_t write(const uint8_t *data,size_t size) {
        write_requests.emplace_back(reinterpret_cast<const char *>(data),size);if(on_write)on_write();
        if(!connected || reject_write || size>static_cast<size_t>(space)) return 0;
        const size_t count=write_limit>=0&&static_cast<size_t>(write_limit)<size?static_cast<size_t>(write_limit):size;
        output.append(reinterpret_cast<const char *>(data),count); if(spaceAfterWrite>=0)space=spaceAfterWrite; return count;
    }
    void flush() { if(on_flush) on_flush(); }
    void clear() {
        input.clear();output.clear();write_requests.clear();on_write=nullptr;on_flush=nullptr;
        connected=true;reject_write=false;endlessly_readable=false;reject_read=false;
        read_bytes=read_calls=0;read_budget=static_cast<size_t>(-1);space=127;spaceAfterWrite=-1;write_limit=-1;
    }
private:
    static int portAvailable(void *context) { return static_cast<Stream *>(context)->available(); }
    static size_t portRead(void *context,char *data,size_t size) { return static_cast<Stream *>(context)->readBytes(data,size); }
    static size_t portWrite(void *context,const uint8_t *data,size_t size) { return static_cast<Stream *>(context)->write(data,size); }
    static int portWriteSpace(void *context) { return static_cast<Stream *>(context)->availableForWrite(); }
    static bool portConnected(void *context) { return static_cast<Stream *>(context)->isConnected(); }
    static void portDrain(void *context) { static_cast<Stream *>(context)->flush(); }
};
using USBSerial = Stream;
class HardwareSerial : public Stream {public: HardwareSerial(int,int) {space=4096;} void clear(){Stream::clear();space=4096;}};
extern Stream Serial;
class TwoWire {public: void begin() {}};
extern TwoWire Wire;
#endif
