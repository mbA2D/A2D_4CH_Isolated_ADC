#ifndef SESSION_TEST_ARDUINO_H
#define SESSION_TEST_ARDUINO_H
#include <stdint.h>
struct PCD_HandleTypeDef { void *Instance; };
extern unsigned aborts, completions_cleared;
extern unsigned rx_completions_cleared;
extern bool pending_rx;
inline void HAL_PCD_EP_Abort(PCD_HandleTypeDef *, unsigned) { ++aborts; }
#define PCD_CLEAR_TX_EP_CTR(device, ep) (++completions_cleared)
#define PCD_CLEAR_RX_EP_CTR(device, ep) (pending_rx=false, ++rx_completions_cleared)
#endif
