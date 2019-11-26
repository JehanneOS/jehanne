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

Waitmsg*
jehanne_wait(void)
{
	int n, l;
	char buf[512], *fld[5];
	Waitmsg *w;

	n = sys_await(buf, sizeof buf-1);
	if(n < 0)
		return nil;
	buf[n] = '\0';
	if(jehanne_tokenize(buf, fld, nelem(fld)) != nelem(fld)){
		jehanne_werrstr("couldn't parse wait message");
		return nil;
	}
	l = jehanne_strlen(fld[4])+1;
	w = jehanne_malloc(sizeof(Waitmsg)+l);
	if(w == nil)
		return nil;
	w->pid = jehanne_atoi(fld[0]);
	w->time[0] = jehanne_atoi(fld[1]);
	w->time[1] = jehanne_atoi(fld[2]);
	w->time[2] = jehanne_atoi(fld[3]);
	jehanne_memmove(w->msg, fld[4], l);
	return w;
}

