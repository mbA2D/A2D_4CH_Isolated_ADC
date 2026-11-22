#ifndef A2D_DISPATCHER_H
#define A2D_DISPATCHER_H
#include <A2D_4CH_Isolated_ADC.h>
#include "A2D_Transport.h"
namespace a2d {
class Dispatcher {
public:
    Dispatcher(A2D_4CH_Isolated_ADC &board, Transport &transport, const char *serial)
        : board_(board), transport_(transport), serial_(serial) {}
    void execute(const Request &request);
private:
    void read(const Request &request, uint8_t channel);
    A2D_4CH_Isolated_ADC &board_;
    Transport &transport_;
    const char *serial_;
};
}
#endif
