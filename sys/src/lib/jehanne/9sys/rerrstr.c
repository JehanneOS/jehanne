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

void
jehanne_rerrstr(char *buf, uint32_t nbuf)
{
	char tmp[ERRMAX];

	tmp[0] = 0;
	sys_errstr(tmp, sizeof tmp);
	jehanne_utfecpy(buf, buf+nbuf, tmp);
	sys_errstr(tmp, sizeof tmp);
}
