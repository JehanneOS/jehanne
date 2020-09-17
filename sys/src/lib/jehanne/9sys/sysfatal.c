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


static void
_sysfatalimpl(const char *fmt, va_list arg)
{
	char buf[1024];

	jehanne_vseprint(buf, buf+sizeof(buf), fmt, arg);
	if(argv0)
		jehanne_fprint(2, "%s: %s\n", argv0, buf);
	else
		jehanne_fprint(2, "%s\n", buf);
	jehanne_exits(buf);
}

void (*_sysfatal)(const char *fmt, va_list arg) = _sysfatalimpl;

void
jehanne_sysfatal(const char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	(*_sysfatal)(fmt, arg);
	va_end(arg);
	jehanne_exits("sysfatal");
}
