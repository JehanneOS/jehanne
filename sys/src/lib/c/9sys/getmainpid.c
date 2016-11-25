/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
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

extern int32_t _mainpid;	/* declared in $ARCH/argv0.c
							 * set by $ARCH/main9.S and $ARCH/syscall.c
							 */

int32_t
getmainpid(void)
{
	/* getmainpid returns the pid of the process at the top of the 
	 * stack, that is the process that started the main() function.
	 *
	 * it replace the old Plan9 _tos->pid
	 */
	return _mainpid;
}
