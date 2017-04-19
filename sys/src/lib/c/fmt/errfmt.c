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

int
jehanne_errfmt(Fmt *f)
{
#if __STDC_HOSTED__
	char buf[ERRMAX+1];

	jehanne_rerrstr(buf, ERRMAX);
	return _fmtcpy(f, buf, jehanne_utflen(buf), jehanne_strlen(buf));
#else
	extern void panic(char *fmt, ...) __attribute__ ((noreturn));
	panic("No errfmt in kernel");
#endif
}
