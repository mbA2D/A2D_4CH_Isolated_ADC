#include "main.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include <string.h>
extern USBD_DescriptorsTypeDef A2D_Descriptors;
USBD_HandleTypeDef hUsbDevice;
static uint8_t rx_packet[64],rx[512],tx[128];
static volatile uint16_t rx_read,rx_write;
static volatile bool opened,armed;
static volatile uint32_t generation;
uint32_t A2D_usb_irq_lock(void) {
    const uint32_t enabled=NVIC_GetEnableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);__DSB();__ISB();return enabled;
}
void A2D_usb_irq_unlock(uint32_t enabled) { if(enabled)NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn); }
static unsigned rx_count(void) { return (rx_write-rx_read)&511U; }
static void resume_rx(void) {
    if(armed || 511U-rx_count()<64U)return;
    armed=true;USBD_CDC_SetRxBuffer(&hUsbDevice,rx_packet);
    if(USBD_CDC_ReceivePacket(&hUsbDevice)!=USBD_OK)armed=false;
}
static void session(bool open) {
    // EP0 can run before an already ACKed bulk completion. Cancel both hardware
    // directions before publishing the new session, without resetting toggles.
    HAL_PCD_EP_Abort(&hpcd,CDC_OUT_EP);PCD_CLEAR_RX_EP_CTR(hpcd.Instance,CDC_OUT_EP&15U);
    HAL_PCD_EP_Abort(&hpcd,CDC_IN_EP);PCD_CLEAR_TX_EP_CTR(hpcd.Instance,CDC_IN_EP&15U);
    USBD_CDC_HandleTypeDef *cdc=(USBD_CDC_HandleTypeDef *)hUsbDevice.pClassData;
    if(cdc) { cdc->TxState=0;cdc->TxLength=0; }
    hUsbDevice.ep_in[CDC_IN_EP&15U].total_length=0;
    rx_read=rx_write=0;armed=false;
    opened=open;++generation;resume_rx();
}
uint32_t A2D_usb_session_generation(void) { return generation; }
bool A2D_usb_session_open(void) { return opened && hUsbDevice.dev_state==USBD_STATE_CONFIGURED; }
static int8_t init(void) {
    opened=false;++generation;rx_read=rx_write=0;armed=false;
    USBD_CDC_SetRxBuffer(&hUsbDevice,rx_packet);
    // The CDC class arms the first OUT receive after its Init callback returns.
    armed=true;return USBD_OK;
}
static int8_t deinit(void) { opened=false;armed=false;++generation;return USBD_OK; }
static uint8_t line_coding[7]={0x00,0xC2,0x01,0,0,0,8};
static int8_t control(uint8_t command,uint8_t *data,uint16_t length) {
    (void)length;
    if(command==CDC_SET_CONTROL_LINE_STATE) {
        bool next=(((USBD_SetupReqTypedef *)data)->wValue&1U)!=0;
        if(next!=opened)session(next);
    } else if(command==CDC_SET_LINE_CODING)memcpy(line_coding,data,7);
    else if(command==CDC_GET_LINE_CODING)memcpy(data,line_coding,7);
    return USBD_OK;
}
static int8_t receive(uint8_t *data,uint32_t *size) {
    armed=false;
    if(opened) {
        // A packet is only armed when64 free bytes exist. Never arm length0.
        for(uint32_t i=0;i<*size;++i) { rx[rx_write]=data[i];rx_write=(rx_write+1)&511U; }
    }
    resume_rx();return USBD_OK;
}
static USBD_CDC_ItfTypeDef interface={init,deinit,control,receive};
int A2D_usb_available(void) { return (int)rx_count(); }
size_t A2D_usb_read(uint8_t *data,size_t size) {
    uint32_t mask=A2D_usb_irq_lock();size_t count=0;
    while(count<size && rx_read!=rx_write) { data[count++]=rx[rx_read];rx_read=(rx_read+1)&511U; }
    if(hUsbDevice.dev_state==USBD_STATE_CONFIGURED)resume_rx();
    A2D_usb_irq_unlock(mask);return count;
}
int A2D_usb_write_space(void) {
    USBD_CDC_HandleTypeDef *cdc=(USBD_CDC_HandleTypeDef *)hUsbDevice.pClassData;
    return A2D_usb_session_open() && cdc && cdc->TxState==0?127:0;
}
size_t A2D_usb_write(const uint8_t *data,size_t size) {
    if(size>127 || !A2D_usb_write_space())return 0;
    memcpy(tx,data,size);
    USBD_CDC_SetTxBuffer(&hUsbDevice,tx,(uint32_t)size);
    if(USBD_CDC_TransmitPacket(&hUsbDevice)!=USBD_OK)return 0;
    return size;
}
void A2D_usb_init(void) {
    // ST's example CDC descriptor defaults to self-powered. This board draws
    // USB power; keep configuration attributes consistent with GET_STATUS.
    uint16_t descriptor_size;
    uint8_t *descriptor=USBD_CDC.GetFSConfigDescriptor(&descriptor_size);
    if(descriptor_size<9)Error_Handler();
    descriptor[7]=0x80;
    // The board's fixed pull-up needs a physical disconnect on startup too.
    GPIO_InitTypeDef pin={0};pin.Pin=GPIO_PIN_12;pin.Mode=GPIO_MODE_OUTPUT_OD;pin.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_12,GPIO_PIN_RESET);HAL_GPIO_Init(GPIOA,&pin);HAL_Delay(100);
    if(USBD_Init(&hUsbDevice,&A2D_Descriptors,0)!=USBD_OK ||
       USBD_RegisterClass(&hUsbDevice,&USBD_CDC)!=USBD_OK ||
       USBD_CDC_RegisterInterface(&hUsbDevice,&interface)!=USBD_OK || USBD_Start(&hUsbDevice)!=USBD_OK)Error_Handler();
}
