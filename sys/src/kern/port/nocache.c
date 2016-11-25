#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

// No file caching

void
cinit(void)
{
}

void
copen(Chan *c)
{
	USED(c);
}

int
cread(Chan *c, uint8_t *buf, int len, int64_t off)
{
	USED(c, buf, len, off);
	return 0;
}

void
cupdate(Chan *c, uint8_t *buf, int len, int64_t off)
{
	USED(c, buf, len, off);
}

void
cwrite(Chan* c, uint8_t *buf, int len, int64_t off)
{
	USED(c, buf, len, off);
}
