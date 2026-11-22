#ifndef A2D_APPLICATION_H
#define A2D_APPLICATION_H
#include "A2D_Dispatcher.h"
namespace a2d {
class Application {
public:
    Application(A2D_4CH_Isolated_ADC &board, Transport &transport, Dispatcher &dispatcher)
        : board_(board), transport_(transport), dispatcher_(dispatcher) {}
    void poll();
private:
    A2D_4CH_Isolated_ADC &board_;
    Transport &transport_;
    Dispatcher &dispatcher_;
};
}
#endif
