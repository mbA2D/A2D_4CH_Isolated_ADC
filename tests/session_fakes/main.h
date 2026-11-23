#ifndef A2D_USB_TEST_MAIN_H
#define A2D_USB_TEST_MAIN_H
#include "A2D_HAL.h"
#include <string.h>
#include <stdlib.h>
#define USBD_OK 0
#define USBD_STATE_CONFIGURED 3
#define CDC_IN_EP 0x81
#define CDC_OUT_EP 1
#define CDC_SET_CONTROL_LINE_STATE 0x22
#define CDC_SET_LINE_CODING 0x20
#define CDC_GET_LINE_CODING 0x21
#define GPIO_PIN_12 12
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_SPEED_FREQ_LOW 1
#define GPIO_PIN_RESET 0
#define GPIOA 0
static uint32_t usb_irq_enabled=1;
#define USB_LP_CAN1_RX0_IRQn 20
static inline uint32_t NVIC_GetEnableIRQ(int irq){(void)irq;return usb_irq_enabled;}
static inline void NVIC_DisableIRQ(int irq){(void)irq;usb_irq_enabled=0;}
static inline void NVIC_EnableIRQ(int irq){(void)irq;usb_irq_enabled=1;}
#define __DSB() ((void)0)
#define __ISB() ((void)0)
typedef struct {unsigned Pin,Mode,Speed;} GPIO_InitTypeDef;
typedef struct {uint16_t wValue;} USBD_SetupReqTypedef;
typedef struct {uint32_t TxState,TxLength;} USBD_CDC_HandleTypeDef;
typedef struct {unsigned unused;} USBD_DescriptorsTypeDef;
typedef struct {uint32_t total_length;} Endpoint;
typedef struct {void *pClassData;int dev_state;Endpoint ep_in[16];} USBD_HandleTypeDef;
typedef struct {void *Instance;} PCD_HandleTypeDef;
typedef struct {int8_t(*Init)(void);int8_t(*DeInit)(void);int8_t(*Control)(uint8_t,uint8_t*,uint16_t);int8_t(*Receive)(uint8_t*,uint32_t*);} USBD_CDC_ItfTypeDef;
extern PCD_HandleTypeDef hpcd;
extern USBD_CDC_HandleTypeDef cdc;
extern unsigned aborts,rx_clears,tx_clears,arms;
extern uint8_t *armed_buffer;
extern bool pending_rx;
extern USBD_CDC_ItfTypeDef *registered_interface;
static uint8_t configuration[9]={9,2,0,0,0,0,0,0xc0,50};
static inline uint8_t *get_configuration(uint16_t *size){*size=sizeof(configuration);return configuration;}
typedef struct {uint8_t *(*GetFSConfigDescriptor)(uint16_t*);} FakeClass;
static FakeClass USBD_CDC={get_configuration};
static inline void HAL_PCD_EP_Abort(PCD_HandleTypeDef *p,unsigned ep){(void)p;(void)ep;++aborts;}
#define PCD_CLEAR_RX_EP_CTR(instance,ep) do{++rx_clears;pending_rx=false;}while(0)
#define PCD_CLEAR_TX_EP_CTR(instance,ep) do{++tx_clears;}while(0)
static inline int USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *d,uint8_t *b){(void)d;armed_buffer=b;return 0;}
static inline int USBD_CDC_ReceivePacket(USBD_HandleTypeDef *d){(void)d;++arms;return 0;}
static inline int USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *d,uint8_t *b,uint32_t n){(void)d;(void)b;cdc.TxLength=n;return 0;}
static inline int USBD_CDC_TransmitPacket(USBD_HandleTypeDef *d){(void)d;cdc.TxState=1;return 0;}
static inline void HAL_GPIO_WritePin(int a,int b,int c){(void)a;(void)b;(void)c;}
static inline void HAL_GPIO_Init(int a,GPIO_InitTypeDef *b){(void)a;(void)b;}
static inline int USBD_Init(USBD_HandleTypeDef *d,USBD_DescriptorsTypeDef *desc,int id){(void)desc;(void)id;d->pClassData=&cdc;return 0;}
static inline int USBD_RegisterClass(USBD_HandleTypeDef *d,FakeClass *c){(void)d;(void)c;return 0;}
static inline int USBD_CDC_RegisterInterface(USBD_HandleTypeDef *d,USBD_CDC_ItfTypeDef *i){(void)d;registered_interface=i;return 0;}
static inline int USBD_Start(USBD_HandleTypeDef *d){(void)d;return 0;}
static inline void Error_Handler(void){abort();}
static inline void HAL_Delay(uint32_t ms){(void)ms;}
#endif
