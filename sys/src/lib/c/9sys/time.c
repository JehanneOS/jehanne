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

int32_t
jehanne_time(int32_t *tp)
{
	int64_t t;

	t = jehanne_nsec()/1000000000LL;
	if(tp != nil)
		*tp = t;
	return t;
}
