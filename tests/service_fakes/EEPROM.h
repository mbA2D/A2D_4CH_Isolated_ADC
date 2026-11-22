#ifndef A2D_SERVICE_FAKE_EEPROM_H
#define A2D_SERVICE_FAKE_EEPROM_H
#include <stdint.h>
#include <string.h>
#include <stdexcept>
class TestEEPROM {
public:
    uint8_t bytes[1024];
    unsigned int writes = 0;
    TestEEPROM() { memset(bytes, 0xff, sizeof(bytes)); }
    template<class T> void get(int address, T &value) {
        if (address < 0 || address + sizeof(T) > sizeof(bytes)) throw std::runtime_error("EEPROM read bounds");
        memcpy(&value, bytes + address, sizeof(T));
    }
    template<class T> void put(int address, const T &value) {
        if (address < 0 || address + sizeof(T) > sizeof(bytes)) throw std::runtime_error("EEPROM write bounds");
        memcpy(bytes + address, &value, sizeof(T)); ++writes;
    }
};
extern TestEEPROM EEPROM;
#endif
