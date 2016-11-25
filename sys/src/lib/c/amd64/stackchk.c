/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
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

#include <u.h>
#include <libc.h>

#if defined(__SSP__) || defined(__SSP_ALL__) || defined(__SSP_STRONG__)

#define STACK_CHK_GUARD 0x2101170925071803
uintptr_t __stack_chk_guard = STACK_CHK_GUARD;
__attribute__((noreturn))
void
__stack_chk_fail(void)
{
#if __STDC_HOSTED__
	abort();
	__builtin_unreachable();
#else
	extern void panic(char *fmt, ...) __attribute__ ((noreturn));
	panic("Stack smashing detected");
#endif
}

#else /* no stack protector */

uintptr_t __stack_chk_guard = 0;

#endif
