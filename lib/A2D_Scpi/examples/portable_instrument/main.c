/* Each argument is one writable, complete command without its LF terminator. */
#include "Instrument.h"
#include <stdio.h>

static void print_reply(void *context, const char *line) {
    (void)context;
    /* Console text mode supplies native newlines on Windows. */
    for (; *line; ++line) if (*line != '\r') fputc(*line, stdout);
}

int main(int argc, char **argv) {
    Instrument instrument;
    instrument_init(&instrument);
    for (int i = 1; i < argc; ++i) instrument_execute(&instrument, argv[i], print_reply, NULL);
    return 0;
}
