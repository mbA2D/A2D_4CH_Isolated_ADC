#ifndef EXAMPLE_INSTRUMENT_H
#define EXAMPLE_INSTRUMENT_H

#include <stdbool.h>
#include <A2D_ScpiErrorQueue.h>

/* A second project, without ADC, Arduino, STM32 or RS485 dependencies. */
typedef void (*InstrumentReply)(void *context, const char *line);
typedef struct {
    bool enabled;
    float limit;
    A2D_ScpiErrorQueue errors;
} Instrument;

void instrument_init(Instrument *instrument);
void instrument_execute(Instrument *instrument, char *line, InstrumentReply reply, void *context);
#endif
