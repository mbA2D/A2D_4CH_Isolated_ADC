#include "A2D_Platform.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static char text[128]; static size_t length;
static size_t append(void *context, const uint8_t *data, size_t count) {
    (void)context;
    if (length + count >= sizeof(text)) abort();
    memcpy(text + length, data, count); length += count; text[length] = 0; return count;
}
int main(void) {
    A2D_Writer writer = {NULL, append};
    const struct { float value; unsigned digits; const char *expected; } cases[] = {
        {0.09375f,4,"0.0938"},{-0.09375f,4,"-0.0938"},
        {0.046875f,5,"0.04688"},{-0.046875f,5,"-0.04688"},
        {0.0f,5,"0.00000"},{-0.0f,4,"0.0000"},{2.818181818f,5,"2.81818"},
        {0.999999f,4,"1.0000"},{1.5f,0,"2"},
        {4294967040.0f,4,"4294967040.0000"},{-4294967040.0f,5,"-4294967040.00000"},
        {NAN,4,""},{INFINITY,4,""},{1e20f,4,""},{1.0f,6,""}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        length=0;text[0]=0;a2d_writer_float(&writer,cases[i].value,cases[i].digits);
        if(strcmp(text,cases[i].expected)) {fprintf(stderr,"format case%u: %s != %s\n",(unsigned)i,text,cases[i].expected);return 1;}
    }
    length=0;text[0]=0;a2d_writer_uint(&writer,UINT32_MAX);a2d_writer_newline(&writer);
    if(strcmp(text,"4294967295\r\n"))return 1;
    puts("16 C numeric formatting cases passed");return 0;
}
