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
getpid(void)
{
	char buf[20];
	int pid;

	pid = open("#c/pid", OREAD);
	if(pid < 0){
		// probably rfork(RFNOMNT)
		pid = open("/dev/pid", OREAD);
		if(pid < 0){
			werrstr("getpid: cannot open neither #c/pid nor /dev/pid");
			return -1;	// probably rfork(RFNOMNT|RFCNAMEG)
		}
	}
	memset(buf, 0, sizeof(buf));
	assert(read(pid, buf, sizeof(buf)) > 0);
	close(pid);
	return atol(buf);
}
