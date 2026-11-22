// Standalone PC demonstration. Each argument is one complete command (no LF).
#include "Instrument.h"
#include <stdio.h>

void printReply(void *, const char *line) {
    // Console text mode supplies native newlines; avoid doubling CR on Windows.
    for (; *line; ++line) if (*line != '\r') fputc(*line, stdout);
}

int main(int argc, char **argv) {
    Instrument instrument;
    for (int i = 1; i < argc; ++i) instrument.execute(argv[i], printReply, nullptr);
}
