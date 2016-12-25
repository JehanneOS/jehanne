/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
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
access(const char *name, int mode)
{
	int fd;

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

	fd = open(name, omode[mode&AMASK]);
	if(fd >= 0){
		close(fd);
		return 0;
	}
	return -1;
}
