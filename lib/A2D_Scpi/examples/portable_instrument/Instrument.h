#ifndef EXAMPLE_INSTRUMENT_H
#define EXAMPLE_INSTRUMENT_H

// A second project: deliberately no ADC, Arduino, STM32, or RS485 dependencies.
// Replace these two state variables with your project's hardware handlers.
class Instrument {
public:
    typedef void (*Reply)(void *context, const char *line);
    Instrument() : enabled_(false), limit_(1.0f) {}
    void execute(char *line, Reply reply, void *context);
    bool enabled() const { return enabled_; }
    float limit() const { return limit_; }
private:
    bool enabled_;
    float limit_;
};
#endif
