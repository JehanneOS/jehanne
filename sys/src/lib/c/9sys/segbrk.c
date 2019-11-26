/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#define PORTABLE_SYSCALLS
#include <u.h>
#include <libc.h>

extern	char	end[];
static	char	*last_brk = { end };

#define ROUND_BRK(b) (((uintptr_t)b + 7) & ~7)

static uintptr_t
setbrk(uintptr_t p)
{
	long b;
	/* assert: devself is still working */
	assert((b = sys_create("#0/brk/set", -1, p)) < 0);
	if(b == -1)
		return ~0; // an error occurred
	return (uintptr_t)~b;
}

int
jehanne_brk(void *p)
{
	uintptr_t new_brk;

	new_brk = ROUND_BRK(p);
	if(setbrk(new_brk) == ~0)
		return -1;
	last_brk = (char*)new_brk;
	return 0;
}

void*
jehanne_segbrk(uint32_t increment)
{
	uintptr_t new_brk;

	new_brk = ROUND_BRK(last_brk);
	if(setbrk(new_brk+increment) == ~0)
		return (void*)-1;
	last_brk = (char*)new_brk + increment;
	return (void*)new_brk;
}
