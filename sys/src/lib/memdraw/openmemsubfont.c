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
#include <draw.h>
#include <memdraw.h>

Memsubfont*
openmemsubfont(char *name)
{
	Memsubfont *sf;
	Memimage *i;
	Fontchar *fc;
	int fd, n;
	char hdr[3*12+4+1];
	uint8_t *p;

	fd = sys_open(name, OREAD);
	if(fd < 0)
		return nil;
	p = nil;
	i = readmemimage(fd);
	if(i == nil)
		goto Err;
	if(jehanne_read(fd, hdr, 3*12) != 3*12){
		jehanne_werrstr("openmemsubfont: header read error: %r");
		goto Err;
	}
	n = jehanne_atoi(hdr);
	p = jehanne_malloc(6*(n+1));
	if(p == nil)
		goto Err;
	if(jehanne_read(fd, p, 6*(n+1)) != 6*(n+1)){
		jehanne_werrstr("openmemsubfont: fontchar read error: %r");
		goto Err;
	}
	fc = jehanne_malloc(sizeof(Fontchar)*(n+1));
	if(fc == nil)
		goto Err;
	_unpackinfo(fc, p, n);
	sf = allocmemsubfont(name, n, jehanne_atoi(hdr+12), jehanne_atoi(hdr+24), fc, i);
	if(sf == nil){
		jehanne_free(fc);
		goto Err;
	}
	jehanne_free(p);
	return sf;
Err:
	sys_close(fd);
	if (i != nil)
		freememimage(i);
	if (p != nil)
		jehanne_free(p);
	return nil;
}
