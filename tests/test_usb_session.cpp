#define ARDUINO_ARCH_STM32
#include <iostream>
#include <stdexcept>
#include "session_fakes/Arduino.h"
#include "session_fakes/usbd_cdc_if.h"

unsigned aborts=0,completions_cleared=0,resumed=0,initializations=0,deinitializations=0,received=0;
unsigned rx_completions_cleared=0;bool pending_rx=false;
RxQueue ReceiveQueue={0,0,192};TxQueue TransmitQueue={0,0,0};
USBD_CDC_HandleTypeDef cdc={1,64};PCD_HandleTypeDef pcd={nullptr};
extern "C" {
USBD_HandleTypeDef hUSBD_Device_CDC={&cdc,&pcd,{}};
volatile bool dtrState=false;
volatile bool receivePended=true;
}
int8_t originalInit(){++initializations;return 0;}
int8_t originalDeInit(){++deinitializations;return 0;}
int8_t originalControl(uint8_t cmd,uint8_t *data,uint16_t){if(cmd==CDC_SET_CONTROL_LINE_STATE)dtrState=(*data&1)!=0;return 0;}
int8_t originalReceive(uint8_t *,uint32_t *length){received+=*length;ReceiveQueue.write+=*length;return 0;}
int8_t originalComplete(uint8_t *,uint32_t *,uint8_t){return 0;}
USBD_CDC_ItfTypeDef USBD_CDC_fops={originalInit,originalDeInit,originalControl,originalReceive,originalComplete};
#include "../A2D_USB_Session.cpp"
#define CHECK(value) do{if(!(value))throw std::runtime_error(#value);}while(0)
void controlLine(uint8_t state){USBD_CDC_fops.Control(CDC_SET_CONTROL_LINE_STATE,&state,0);}
int main(){try{
    A2D_install_usb_session_hooks();A2D_install_usb_session_hooks();USBD_CDC_fops.Init();
    CHECK(initializations==1&&!A2D_usb_session_open()&&A2D_usb_session_generation()==1);
    CHECK(USBD_CDC_fops.TransmitCplt==originalComplete);
    std::cout<<"PASS session hooks install once and start closed\n";

    ReceiveQueue={3,64,128};TransmitQueue={1,100,63};hUSBD_Device_CDC.ep_in[1].total_length=64;
    controlLine(1);
    CHECK(A2D_usb_session_open()&&A2D_usb_session_generation()==2);
    CHECK(ReceiveQueue.read==64&&ReceiveQueue.write==64&&ReceiveQueue.length==128);
    CHECK(TransmitQueue.write==0&&TransmitQueue.read==0&&cdc.TxState==0&&cdc.TxLength==0);
    CHECK(aborts==2&&completions_cleared==1&&rx_completions_cleared==1&&resumed==1&&receivePended&&hUSBD_Device_CDC.ep_in[1].total_length==0);
    ReceiveQueue.write=65;controlLine(3); // RTS-only changes preserve a live session.
    CHECK(A2D_usb_session_generation()==2&&ReceiveQueue.read==64&&aborts==2);
    std::cout<<"PASS DTR edges purge queued traffic without invalidating reserved RX\n";

    const auto old=A2D_usb_session_generation();controlLine(0);
    uint8_t data[8]={};uint32_t size=8;USBD_CDC_fops.Receive(data,&size);
    CHECK(size==0&&received==0&&!A2D_usb_session_open());
    pending_rx=true; // Closed-session packet ACKed into PMA; callback still pending.
    controlLine(1);CHECK(A2D_usb_session_generation()==old+2&&!pending_rx);
    CHECK(rx_completions_cleared==3); // Late old OUT completion cannot run after open.
    size=8;USBD_CDC_fops.Receive(data,&size);CHECK(size==8&&received==8);
    std::cout<<"PASS closed-session bytes drop and rapid reopen events survive\n";

    USBD_CDC_fops.DeInit();CHECK(deinitializations==1&&!A2D_usb_session_open());
    const auto deinitialized=A2D_usb_session_generation();USBD_CDC_fops.Init();
    CHECK(initializations==2&&!A2D_usb_session_open()&&A2D_usb_session_generation()==deinitialized+1);
    std::cout<<"PASS USB deconfiguration and reconfiguration end the session\n";
}catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
std::cout<<"4 USB session groups passed\n";}
