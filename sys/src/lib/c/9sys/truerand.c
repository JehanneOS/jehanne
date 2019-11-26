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

uint32_t
jehanne_truerand(void)
{
	uint32_t x;
	static int randfd = -1;

	if(randfd < 0)
		randfd = sys_open("/dev/random", OREAD|OCEXEC);
	if(randfd < 0)
		jehanne_sysfatal("can't open /dev/random");
	if(jehanne_read(randfd, &x, sizeof(x)) != sizeof(x))
		jehanne_sysfatal("can't read /dev/random");
	return x;
}
