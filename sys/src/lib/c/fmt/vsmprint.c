/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include "fmtdef.h"

static int
fmtStrFlush(Fmt *f)
{
	char *s;
	int n;

	if(f->start == nil)
		return 0;
	n = (int)(uintptr_t)f->farg;
	n *= 2;
	s = f->start;
	f->start = jehanne_realloc(s, n);
	if(f->start == nil){
		f->farg = nil;
		f->to = nil;
		f->stop = nil;
		jehanne_free(s);
		return 0;
	}
	f->farg = (void*)(uintptr_t)n;
	f->to = (char*)f->start + ((char*)f->to - s);
	f->stop = (char*)f->start + n - 1;
	return 1;
}

int
jehanne_fmtstrinit(Fmt *f)
{
	int n;

	jehanne_memset(f, 0, sizeof *f);
	f->runes = 0;
	n = 32;
	f->start = jehanne_malloc(n);
	if(f->start == nil)
		return -1;
	jehanne_setmalloctag(f->start, jehanne_getcallerpc());
	f->to = f->start;
	f->stop = (char*)f->start + n - 1;
	f->flush = fmtStrFlush;
	f->farg = (void*)(uintptr_t)n;
	f->nfmt = 0;
	return 0;
}

/*
 * print into an allocated string buffer
 */
char*
jehanne_vsmprint(const char *fmt, va_list args)
{
	Fmt f;
	int n;

	if(jehanne_fmtstrinit(&f) < 0)
		return nil;
	//f.args = args;
	va_copy(f.args,args);
	n = jehanne_dofmt(&f, fmt);
	va_end(f.args);
	if(f.start == nil)		/* realloc failed? */
		return nil;
	if(n < 0){
		jehanne_free(f.start);
		return nil;
	}
	jehanne_setmalloctag(f.start, jehanne_getcallerpc());
	*(char*)f.to = '\0';
	return f.start;
}
