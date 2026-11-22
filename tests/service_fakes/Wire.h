#ifndef A2D_SERVICE_FAKE_WIRE_H
#define A2D_SERVICE_FAKE_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
class TwoWire {
public:
    uint8_t config[4] = {};
    int16_t codes[4] = {};
    unsigned int transmissions=0,resets=0,triggers=0,requests=0;
    bool failWrites=false,holdNotReady=false,wrongConfig=false;
    uint8_t responseCount=3,address=0,command=0,response[3]={},position=0;
    void begin() {}
    void beginTransmission(uint8_t value) {address=value;}
    size_t write(uint8_t value) {command=value;return 1;}
    uint8_t endTransmission(bool) {
        ++transmissions;if(failWrites)return 2;
        if(address==0 && command==6){++resets;memset(config,0x10,sizeof(config));}
        else if(address==0 && command==8){++triggers;for(auto &c:config)c&=~0x10;}
        else if(address>=0x68 && address<=0x6b)config[address-0x68]=command&0x7f;
        return 0;
    }
    uint8_t requestFrom(uint8_t target,uint8_t) {
        ++requests;const uint8_t ch=target-0x68;const uint16_t code=static_cast<uint16_t>(codes[ch]);
        response[0]=static_cast<uint8_t>(code>>8);response[1]=static_cast<uint8_t>(code);
        response[2]=config[ch]|(holdNotReady?0x80:0);if(wrongConfig)response[2]^=1;
        position=0;return responseCount;
    }
    int read(){return position<responseCount?response[position++]:-1;}
};
extern TwoWire Wire;
#endif
