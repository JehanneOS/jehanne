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
#include <draw.h>

Font*
openfont(Display *d, char *name)
{
	Font *fnt;
	int fd, i, n;
	char *buf;
	Dir *dir;

	fd = sys_open(name, OREAD);
	if(fd < 0)
		return 0;

	dir = dirfstat(fd);
	if(dir == nil){
    Err0:
		sys_close(fd);
		return 0;
	}
	n = dir->length;
	free(dir);
	buf = malloc(n+1);
	if(buf == 0)
		goto Err0;
	buf[n] = 0;
	i = jehanne_read(fd, buf, n);
	sys_close(fd);
	if(i != n){
		free(buf);
		return 0;
	}
	fnt = buildfont(d, buf, name);
	free(buf);
	return fnt;
}
