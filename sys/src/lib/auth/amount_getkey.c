#include <u.h>
#include <lib9.h>
#include <auth.h>

int (*amount_getkey)(char*) = auth_getkey;
