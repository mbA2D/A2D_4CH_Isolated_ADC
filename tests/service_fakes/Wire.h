#ifndef A2D_SERVICE_FAKE_WIRE_H
#define A2D_SERVICE_FAKE_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "A2D_Platform.h"
// Host ADC model attached through the production C callback interface.
struct FakeI2c {
    A2D_I2c bus;
    uint8_t config[4] = {};
    int16_t codes[4] = {};
    unsigned int transmissions=0,resets=0,triggers=0,requests=0;
    bool failWrites=false,holdNotReady=false,wrongConfig=false;
    uint8_t responseCount=3;
    FakeI2c() : bus{this,transmit,receive} {}
    static bool transmit(void *context,uint8_t address,uint8_t command) {
        FakeI2c &self=*static_cast<FakeI2c *>(context);
        ++self.transmissions;if(self.failWrites)return false;
        if(address==0 && command==6){++self.resets;memset(self.config,0x10,sizeof(self.config));}
        else if(address==0 && command==8){++self.triggers;for(auto &c:self.config)c&=~0x10;}
        else if(address>=0x68 && address<=0x6b)self.config[address-0x68]=command&0x7f;
        else return false;
        return true;
    }
    static bool receive(void *context,uint8_t address,uint8_t *data,size_t size) {
        FakeI2c &self=*static_cast<FakeI2c *>(context);
        ++self.requests;
        if(address<0x68 || address>0x6b || size!=3 || self.responseCount!=size)return false;
        const uint8_t channel=address-0x68;const uint16_t code=static_cast<uint16_t>(self.codes[channel]);
        data[0]=static_cast<uint8_t>(code>>8);data[1]=static_cast<uint8_t>(code);
        data[2]=self.config[channel]|(self.holdNotReady?0x80:0);if(self.wrongConfig)data[2]^=1;
        return true;
    }
};
#endif
