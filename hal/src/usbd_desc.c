#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32f1xx_hal.h"
static uint8_t device[18]={18,USB_DESC_TYPE_DEVICE,0,2,2,2,0,64,0x83,0x04,0x40,0x57,0,2,1,2,3,1};
static uint8_t language[4]={4,USB_DESC_TYPE_STRING,0x09,0x04};
static uint8_t string[128],serial[26]={26,USB_DESC_TYPE_STRING};
static uint8_t *dev(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;*size=sizeof(device);return device; }
static uint8_t *lang(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;*size=sizeof(language);return language; }
static uint8_t *text(const char *value,uint16_t *size) { USBD_GetString((uint8_t *)value,string,size);return string; }
static uint8_t *manufacturer(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;return text("STMicroelectronics",size); }
static uint8_t *product(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;return text("STM32 Virtual ComPort",size); }
static uint8_t *configuration(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;return text("CDC Config",size); }
static uint8_t *interface(USBD_SpeedTypeDef speed,uint16_t *size) { (void)speed;return text("CDC Interface",size); }
static void hex(uint32_t value,uint8_t *out,unsigned digits) {
    for(unsigned i=0;i<digits;++i) { unsigned n=value>>28;out[2*i]=(uint8_t)(n<10?'0'+n:'A'+n-10);out[2*i+1]=0;value<<=4; }
}
static uint8_t *identity(USBD_SpeedTypeDef speed,uint16_t *size) {
    (void)speed;
    // Preserve STM32duino's UID conversion so Windows keeps the same COM port.
    hex(HAL_GetUIDw0()+HAL_GetUIDw2(),serial+2,8);hex(HAL_GetUIDw1(),serial+18,4);
    *size=sizeof(serial);return serial;
}
USBD_DescriptorsTypeDef A2D_Descriptors={dev,lang,manufacturer,product,identity,configuration,interface};
