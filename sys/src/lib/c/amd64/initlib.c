/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
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

extern void	**_privates;
extern int	_nprivates;

static void call_main(int argc, char *argv[]) __attribute__((noreturn));

/* can be overwritten to do anything before calling main */
extern void __libc_init(int argc, char *argv[]) __attribute__((weak, noreturn));

void
__jehanne_libc_init(int argc, char *argv[])
{
	/* Initialize per process structures on the stack */
	void *privates[NPRIVATES];
	NativeTypes sysargs[6];

	_nprivates = NPRIVATES;
	for(_nprivates = 0; _nprivates < NPRIVATES; ++_nprivates)
		privates[_nprivates] = nil;
	_privates = privates;
	_sysargs = &sysargs[0];

	if(__libc_init != nil)
		__libc_init(argc, argv);
	call_main(argc, argv);
}

static void
call_main(int argc, char *argv[])
{
	extern void main(int argc, char *argv[]);

	main(argc, argv);
	jehanne_exits("main");
}
