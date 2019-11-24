#include <u.h>
#include <libc.h>

void (*_abort)(void);
char *argv0;
int32_t _mainpid;
char *_privates;
char *_nprivates;
