#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include "9p.h"

void*
emalloc9p(uint32_t sz)
{
	void *v;

	if((v = malloc(sz)) == nil) {
		fprint(2, "out of memory allocating %lud\n", sz);
		exits("mem");
	}
	memset(v, 0, sz);
	setmalloctag(v, getcallerpc());
	return v;
}

void*
erealloc9p(void *v, uint32_t sz)
{
	void *nv;

	if((nv = realloc(v, sz)) == nil && sz != 0) {
		fprint(2, "out of memory allocating %lud\n", sz);
		exits("mem");
	}
	if(v == nil)
		setmalloctag(nv, getcallerpc());
	setrealloctag(nv, getcallerpc());
	return nv;
}

char*
estrdup9p(char *s)
{
	char *t;

	if((t = strdup(s)) == nil) {
		fprint(2, "out of memory in strdup(%.10s)\n", s);
		exits("mem");
	}
	setmalloctag(t, getcallerpc());
	return t;
}

