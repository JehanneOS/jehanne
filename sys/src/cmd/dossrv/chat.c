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
#include "iotrack.h"
#include "dat.h"
#include "fns.h"

#define	SIZE	1024
int	chatty;
extern int	doabort;

void
chat(char *fmt, ...)
{
	va_list arg;

	if (!chatty)
		return;
	va_start(arg, fmt);
	jehanne_vfprint(2, fmt, arg);
	va_end(arg);
}

void
panic(char *fmt, ...)
{
	va_list arg;

	jehanne_fprint(2, "%s %d: panic ", argv0, jehanne_getpid());
	va_start(arg, fmt);
	jehanne_vfprint(2, fmt, arg);
	va_end(arg);
	jehanne_fprint(2, ": %r\n");
	if(doabort)
		abort();
	jehanne_exits("panic");
}
