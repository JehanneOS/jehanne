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

/*
 * Format:
  3 r  M    4 (0000000000457def 11 00)   8192      512 /arch/rc/lib/rcmain
 */

int
jehanne_iounit(int fd)
{
	int i, cfd;
	char buf[128], *args[10];

	jehanne_snprint(buf, sizeof buf, "#d/%dctl", fd);
	cfd = sys_open(buf, OREAD);
	if(cfd < 0)
		return 0;
	i = jehanne_read(cfd, buf, sizeof buf-1);
	sys_close(cfd);
	if(i <= 0)
		return 0;
	buf[i] = '\0';
	if(jehanne_tokenize(buf, args, nelem(args)) != nelem(args))
		return 0;
	return jehanne_atoi(args[7]);
}
