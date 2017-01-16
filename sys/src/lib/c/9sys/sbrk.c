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

extern	char	end[];
static	char	*bloc = { end };

enum
{
	Round	= 7
};

uintptr_t
brk_(uintptr_t p)
{
	long f;
	f = create("#0/brk/set", -1, p);
	if(f >= 0){
		/* this should never happen */
		close(f);
		return -1;
	}
	return (uintptr_t)~f;
}

int
brk(void *p)
{
	uintptr_t bl;

	bl = ((uintptr_t)p + Round) & ~Round;
	if(brk_(bl) < 0)
		return -1;
	bloc = (char*)bl;
	return 0;
}

void*
sbrk(uint32_t n)
{
	uintptr_t bl;

	bl = ((uintptr_t)bloc + Round) & ~Round;
	if(brk_(bl+n) < 0)
		return (void*)-1;
	bloc = (char*)bl + n;
	return (void*)bl;
}
