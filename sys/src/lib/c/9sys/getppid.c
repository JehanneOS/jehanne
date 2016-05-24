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

#include	<u.h>
#include	<libc.h>

int
getppid(void)
{
	char buf[20];
	int ppid;

	ppid = open("#c/ppid", OREAD);
	if(ppid < 0){
		// probably rfork(RFNOMNT)
		ppid = open("/dev/ppid", OREAD);
		if(ppid < 0){
			werrstr("getppid: cannot open neither #c/ppid nor /dev/ppid");
			return -1;	// probably rfork(RFNOMNT|RFCNAMEG)
		}
	}
	memset(buf, 0, sizeof(buf));
	assert(read(ppid, buf, sizeof(buf)) > 0);
	close(ppid);
	return atol(buf);
}
