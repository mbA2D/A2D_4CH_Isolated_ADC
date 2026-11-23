#include <stddef.h>
#include <errno.h>
extern char _end,_estack;
void *_sbrk(ptrdiff_t increment) {
    static char *next;
    if(!next)next=&_end;
    if(increment<0 || next+increment>&_estack-2048) { errno=ENOMEM;return (void *)-1; }
    void *previous=next;next+=increment;return previous;
}
