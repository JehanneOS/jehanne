/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>

/*
 * like fprint but be sure to deliver as a single jehanne_write.
 * (fprint uses a small jehanne_write buffer.)
 */
void
xfprint(int fd, char *fmt, ...)
{
	char *s;
	va_list arg;

	va_start(arg, fmt);
	s = vsmprint(fmt, arg);
	va_end(arg);
	if(s == nil)
		sysfatal("smprint: %r");
	jehanne_write(fd, s, strlen(s));
	free(s);
}

void
main(int argc, char **argv)
{
	int fd;
	char dir[512];

	fd = sys_open("/dev/acme/ctl", OWRITE);
	if(fd < 0)
		exits(0);
	getwd(dir, 512);
	if(dir[0]!=0 && dir[strlen(dir)-1]=='/')
		dir[strlen(dir)-1] = 0;
	xfprint(fd, "name %s/-%s\n",  dir, argc > 1 ? argv[1] : "rc");
	xfprint(fd, "dumpdir %s\n", dir);
	exits(0);
}
