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

void (*_jehanne_onexit)(void);

void
jehanne_exits(const char *s)
{
	void _fini(void);

	if(_jehanne_onexit != nil)
		(*_jehanne_onexit)();
	_fini();
	sys__exits(s);
	__builtin_unreachable();
}
