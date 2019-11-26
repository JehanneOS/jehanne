/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016-2017 Giacomo Tesio <giacomo@tesio.it>
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

int
jehanne_access(const char *name, int mode)
{
	int fd, reqmode;

	static char omode[] = {
		OSTAT,
		OEXEC,
		OWRITE,
		ORDWR,
		OREAD,
		OEXEC,  /* 5=4+1 READ|EXEC, EXEC is enough */
		ORDWR,
		ORDWR   /* 7=4+2+1 READ|WRITE|EXEC, ignore EXEC */
	};

	reqmode = omode[mode&AMASK];
	fd = sys_open(name, reqmode);
	if(fd >= 0){
		sys_close(fd);
		return 0;
	}

	/* WARNING:
	 *
	 * In Plan 9 access(AWRITE) and access(AEXEC) in directories
	 * fail despite the actual permission of the directory.
	 *
	 * This is well understood in Plan 9, but it's counter intuitive.
	 *
	 * In Plan 9, to create a file in a directory you need write
	 * permission in the directory. Still you don't need to (and you
	 * cannot) open the directory for writing before calling create.
	 *
	 * To my eyes this is a UNIX inheritance that could be "fixed"
	 * but there are some trade off.
	 */
	return -1;
}
