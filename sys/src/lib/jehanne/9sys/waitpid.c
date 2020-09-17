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
#include <9P2000.h>

int
jehanne_waitpid(void)
{
	int n;
	char buf[512], *fld[5];

	n = sys_await(buf, sizeof buf-1);
	if(n <= 0)
		return -1;
	buf[n] = '\0';
	if(jehanne_tokenize(buf, fld, nelem(fld)) != nelem(fld)){
		jehanne_werrstr("couldn't parse wait message");
		return -1;
	}
	return jehanne_atoi(fld[0]);
}

