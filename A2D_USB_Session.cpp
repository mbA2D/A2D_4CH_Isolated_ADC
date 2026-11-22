#include "A2D_USB_Session.h"
#if defined(ARDUINO_ARCH_STM32)
#include <Arduino.h>
#include <usbd_cdc_if.h>
#include <usbd_desc.h>

extern "C" {
extern USBD_HandleTypeDef hUSBD_Device_CDC;
extern volatile bool dtrState;
extern volatile bool receivePended;
}

namespace {
volatile uint32_t generation = 0;
volatile bool opened = false;
USBD_CDC_ItfTypeDef original;
bool installed = false;

void changeSession(bool open) {
    opened = open;
    ++generation;
}
void discardQueuedTraffic() {
    // Preserve the reserved OUT buffer and its write pointer. Reinitializing
    // the RX ring here would invalidate the buffer already armed in USB PMA.
    // This board uses the F103 single-buffer peripheral. NAK the IN endpoint
    // and retire any pending completion before reusing its software queue.
    auto *cdc = static_cast<USBD_CDC_HandleTypeDef *>(hUSBD_Device_CDC.pClassData);
    auto *pcd = static_cast<PCD_HandleTypeDef *>(hUSBD_Device_CDC.pData);
    if (cdc && pcd) {
        // EP0 control may be serviced before a pending OUT completion. Drop
        // that old PMA packet before opening the new session, then re-arm RX.
        HAL_PCD_EP_Abort(pcd, CDC_OUT_EP);
        PCD_CLEAR_RX_EP_CTR(pcd->Instance, CDC_OUT_EP & 0x0fU);
        HAL_PCD_EP_Abort(pcd, CDC_IN_EP);
        PCD_CLEAR_TX_EP_CTR(pcd->Instance, CDC_IN_EP & 0x0fU);
        hUSBD_Device_CDC.ep_in[CDC_IN_EP & 0x0fU].total_length = 0;
        cdc->TxState = 0;
        cdc->TxLength = 0;
    }
    ReceiveQueue.read = ReceiveQueue.write;
    receivePended = false;
    CDC_resume_receive();
    CDC_TransmitQueue_Init(&TransmitQueue);
}
int8_t init() {
    changeSession(false);
    return original.Init();
}
int8_t deinit() {
    changeSession(false);
    return original.DeInit();
}
int8_t control(uint8_t command, uint8_t *buffer, uint16_t length) {
    const int8_t result = original.Control(command, buffer, length);
    if (command == CDC_SET_CONTROL_LINE_STATE && opened != dtrState) {
        discardQueuedTraffic();
        changeSession(dtrState);
    }
    return result;
}
int8_t receive(uint8_t *buffer, uint32_t *length) {
    if (!opened) *length = 0; // Closed-session bytes cannot form future commands.
    return original.Receive(buffer, length);
}
}
void A2D_install_usb_session_hooks() {
    if (installed) return;
    original = USBD_CDC_fops;
    USBD_CDC_fops.Init = init;
    USBD_CDC_fops.DeInit = deinit;
    USBD_CDC_fops.Control = control;
    USBD_CDC_fops.Receive = receive;
    installed = true;
}
uint32_t A2D_usb_session_generation() { return generation; }
bool A2D_usb_session_open() { return opened; }
#endif
