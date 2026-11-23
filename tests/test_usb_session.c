#include <stdio.h>
#include <stdlib.h>
#include "session_fakes/main.h"
unsigned aborts=0,rx_clears=0,tx_clears=0,arms=0;
uint8_t *armed_buffer=NULL;bool pending_rx=false;
USBD_CDC_ItfTypeDef *registered_interface=NULL;
USBD_DescriptorsTypeDef A2D_Descriptors;
PCD_HandleTypeDef hpcd={NULL};USBD_CDC_HandleTypeDef cdc={0,0};
uint32_t A2D_irq_lock(void){return 0;}
void A2D_irq_unlock(uint32_t mask){(void)mask;}
void A2D_delay(uint32_t ms){(void)ms;}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "../hal/src/usb.c"
#pragma GCC diagnostic pop
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %s\n",#c);exit(1);}}while(0)
void dtr(unsigned value){USBD_SetupReqTypedef q={(uint16_t)value};registered_interface->Control(CDC_SET_CONTROL_LINE_STATE,(uint8_t*)&q,0);}
void packet(unsigned n,uint8_t value){CHECK(armed);memset(armed_buffer,value,n);uint32_t count=n;registered_interface->Receive(armed_buffer,&count);}
int main(void){
 uint32_t outer=A2D_usb_irq_lock(),inner=A2D_usb_irq_lock();
 CHECK(outer==1&&inner==0&&usb_irq_enabled==0);A2D_usb_irq_unlock(inner);CHECK(usb_irq_enabled==0);
 A2D_usb_irq_unlock(outer);CHECK(usb_irq_enabled==1);
 A2D_usb_init();CHECK(configuration[7]==0x80&&configuration[8]==50);
 hUsbDevice.dev_state=USBD_STATE_CONFIGURED;registered_interface->Init();
 CHECK(!A2D_usb_session_open()&&A2D_usb_session_generation()==1);
 packet(8,1);CHECK(A2D_usb_available()==0);dtr(1);CHECK(A2D_usb_session_open()&&aborts==2);
 packet(64,7);CHECK(A2D_usb_available()==64);const uint32_t epoch=A2D_usb_session_generation();
 dtr(3);CHECK(A2D_usb_available()==64&&epoch==A2D_usb_session_generation());
 cdc.TxState=1;cdc.TxLength=64;hUsbDevice.ep_in[1].total_length=64;pending_rx=true;
 dtr(0);dtr(1);CHECK(!pending_rx&&A2D_usb_available()==0&&cdc.TxState==0&&cdc.TxLength==0&&hUsbDevice.ep_in[1].total_length==0);
 CHECK(A2D_usb_session_generation()==epoch+2&&rx_clears==3&&tx_clears==3);
 printf("PASS native CDC session purges old RX/TX and preserves RTS-only changes\n");
 for(unsigned i=0;i<7;++i)packet(64,(uint8_t)i);
 CHECK(A2D_usb_available()==448&&!armed);uint8_t output[512];
 CHECK(A2D_usb_read(output,1)==1&&armed);packet(64,9);CHECK(A2D_usb_available()==511&&!armed);
 CHECK(A2D_usb_read(output,511)==511&&armed);
 CHECK(output[0]==0&&output[62]==0&&output[63]==1&&output[446]==6&&output[447]==9&&output[510]==9);
 for(unsigned i=0;i<100;++i){packet(64,(uint8_t)i);CHECK(A2D_usb_read(output,64)==64);for(unsigned j=0;j<64;++j)CHECK(output[j]==i);}
 printf("PASS full packet reservation, wraparound and resume without zero-length arms\n");
 CHECK(A2D_usb_write_space()==127);CHECK(A2D_usb_write(output,128)==0);CHECK(A2D_usb_write(output,64)==64&&A2D_usb_write_space()==0);
 dtr(0);CHECK(A2D_usb_write(output,1)==0);dtr(1);CHECK(A2D_usb_write_space()==127);
 registered_interface->DeInit();CHECK(!A2D_usb_session_open());registered_interface->Init();CHECK(!A2D_usb_session_open());
 printf("PASS bounded TX and USB deconfiguration\n");
return 0;}
