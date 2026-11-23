#ifndef A2D_DISPATCHER_H
#define A2D_DISPATCHER_H
#include "A2D_Transport.h"
#ifdef __cplusplus
extern "C" {
#endif
void a2d_dispatch_execute(A2D_Transport *transport, const A2D_Request *request);

#ifdef __cplusplus
}
#endif
#endif
