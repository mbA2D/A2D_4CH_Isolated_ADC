#ifndef SESSION_TEST_CDC_H
#define SESSION_TEST_CDC_H
#include <stdint.h>
#define CDC_SET_CONTROL_LINE_STATE 0x22
#define CDC_IN_EP 0x81
#define CDC_OUT_EP 0x01
struct USBD_CDC_ItfTypeDef {
    int8_t (*Init)(); int8_t (*DeInit)();
    int8_t (*Control)(uint8_t, uint8_t *, uint16_t);
    int8_t (*Receive)(uint8_t *, uint32_t *);
    int8_t (*TransmitCplt)(uint8_t *, uint32_t *, uint8_t);
};
struct USBD_CDC_HandleTypeDef { uint32_t TxState, TxLength; };
struct USBD_HandleTypeDef {
    void *pClassData; void *pData;
    struct Endpoint { uint32_t total_length; } ep_in[16];
};
struct RxQueue { unsigned read, write, length; };
struct TxQueue { unsigned read, write, reserved; };
extern USBD_CDC_ItfTypeDef USBD_CDC_fops;
extern RxQueue ReceiveQueue;
extern TxQueue TransmitQueue;
extern unsigned resumed;
extern "C" volatile bool receivePended;
inline void CDC_resume_receive() { receivePended=true; ++resumed; }
inline void CDC_TransmitQueue_Init(TxQueue *q) { q->read=q->write=q->reserved=0; }
#endif
