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

static
char*
skip(char *p)
{

	while(*p == ' ')
		p++;
	while(*p != ' ' && *p != 0)
		p++;
	return p;
}

/*
 *  after a fork with fd's copied, both fd's are pointing to
 *  the same Chan structure.  Since the offset is kept in the Chan
 *  structure, the seek's and read's in the two processes can be
 *  are competing moving the offset around.  Hence the unusual loop
 *  in the middle of this routine.
 */
int32_t
jehanne_times(int32_t *t)
{
	char b[200], *p;
	static int f = -1;
	int i, retries;
	uint32_t r = -1;

	jehanne_memset(b, 0, sizeof(b));
	for(retries = 0; retries < 100; retries++){
		if(f < 0)
			f = sys_open("/dev/cputime", OREAD|OCEXEC);
		if(f < 0)
			break;
		if(sys_seek(f, 0, 0) < 0 || (i = jehanne_read(f, b, sizeof(b))) < 0){
			sys_close(f);
			f = -1;
		} else {
			if(i != 0)
				break;
		}
	}
	p = b;
	if(t)
		t[0] = jehanne_atol(p);
	p = skip(p);
	if(t)
		t[1] = jehanne_atol(p);
	p = skip(p);
	r = jehanne_atol(p);
	if(t){
		p = skip(p);
		t[2] = jehanne_atol(p);
		p = skip(p);
		t[3] = jehanne_atol(p);
	}
	return r;
}
