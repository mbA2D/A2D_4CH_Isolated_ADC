#include "A2D_Platform.h"
#include <string.h>
#include <math.h>
static void byte(A2D_Writer *writer, uint8_t value) {
    writer->write(writer->context, &value, 1);
}
void a2d_writer_text(A2D_Writer *writer, const char *text) {
    writer->write(writer->context, (const uint8_t *)text, strlen(text));
}
void a2d_writer_uint(A2D_Writer *writer, uint32_t value) {
    char digits[10]; unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) byte(writer, (uint8_t)digits[--count]);
}
void a2d_writer_float(A2D_Writer *writer, float value, unsigned digits) {
    if (digits > 5 || !isfinite(value) || fabsf(value) > 4294967040.0f) return;
    double number = value;
    if (number < 0) { byte(writer, '-'); number = -number; }
    uint32_t scale = 1;
    for (unsigned i = 0; i < digits; ++i) scale *= 10;
    /* Round once. Float times this integer scale is exact in binary64. */
    const uint64_t rounded = (uint64_t)(number * scale + 0.5);
    a2d_writer_uint(writer, (uint32_t)(rounded / scale));
    uint32_t fraction = (uint32_t)(rounded % scale);
    if (digits) byte(writer, '.');
    for (uint32_t place = scale / 10; place; place /= 10) {
        byte(writer, (uint8_t)('0' + fraction / place)); fraction %= place;
    }
}
void a2d_writer_newline(A2D_Writer *writer) { a2d_writer_text(writer, "\r\n"); }
