#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#define F(text) text
#define PB11 11
#define PB10 10
extern unsigned long test_clock;
inline unsigned long millis() { return test_clock++; }
inline void delay(unsigned long ms) { test_clock += ms; }
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t *data, size_t size) { size_t count=0; while(count<size && write(data[count])) ++count; return count; }
    virtual int availableForWrite() { return 0; }
    void print(const char *s) { write(reinterpret_cast<const uint8_t *>(s), strlen(s)); }
    void print(unsigned char value) { print(static_cast<unsigned int>(value)); }
    template<class T> void print(T value) { std::ostringstream s; s << value; print(s.str().c_str()); }
    void print(float value, int digits) { std::ostringstream s; s << std::fixed << std::setprecision(digits) << value; print(s.str().c_str()); }
    void println() { print("\r\n"); }
    template<class T> void println(T value) { print(value); println(); }
};
class Stream : public Print {
public:
    std::string input, output;
    std::function<void()> on_flush;
    bool connected = true;
    int space = 127, spaceAfterWrite = -1;
    explicit operator bool() const { return connected; }
    void begin(unsigned long) {}
    int available() { return static_cast<int>(input.size()); }
    int availableForWrite() override { return space; }
    int read() { if(input.empty()) return -1; const unsigned char c=input[0]; input.erase(0,1); return c; }
    size_t readBytes(char *data, size_t size) { const size_t count=size<input.size()?size:input.size(); memcpy(data,input.data(),count); input.erase(0,count); return count; }
    size_t write(uint8_t value) override { return write(&value,1); }
    size_t write(const uint8_t *data,size_t size) override {
        if(!connected || size>static_cast<size_t>(space)) return 0;
        output.append(reinterpret_cast<const char *>(data),size); if(spaceAfterWrite>=0)space=spaceAfterWrite; return size;
    }
    void flush() { if(on_flush) on_flush(); }
    void clear() {input.clear();output.clear();on_flush=nullptr;connected=true;space=127;spaceAfterWrite=-1;}
};
using USBSerial = Stream;
class HardwareSerial : public Stream {public: HardwareSerial(int,int) {space=4096;} void clear(){Stream::clear();space=4096;}};
extern Stream Serial;
class TwoWire {public: void begin() {}};
extern TwoWire Wire;
#endif
