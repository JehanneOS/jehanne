#include "os.h"
#include <libsec.h>

//
//  just use the libc prng to fill a buffer
//
void
prng(uint8_t *p, int n)
{
	uint8_t *e;

	for(e = p+n; p < e; p++)
		*p = rand();
}
