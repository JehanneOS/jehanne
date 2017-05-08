/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>
#include <posix.h>
#include "internal.h"

int
POSIX_isatty(int *errnop, int fd)
{
	char buf[64];
	int l;
	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return 0;
	}
	if(sys_fd2path(fd, buf, sizeof(buf)) < 0){
		*errnop = __libposix_get_errno(PosixENOTTY);
		return 0;
	}
	
	l = strlen(buf);
	if(l >= 9 && strcmp(buf+l-9, "/dev/cons") == 0)
		return 1;
	*errnop = __libposix_get_errno(PosixENOTTY);
	return 0;
}

unsigned int
POSIX_sleep(unsigned int seconds)
{
	sleep(seconds*1000);
	return 0;
}

unsigned int
POSIX_usleep(int *errnop, unsigned int usec)
{
	if(usec >= 1000000){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	unsigned int ms = usec == 0 ? 0 : (usec+1000)/1000;
	sleep(ms);
	return 0;
}


clock_t
POSIX_times(int *errnop, void *buf)
{
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}

int
POSIX_gettimeofday(int *errnop, void *timeval, void *timezone)
{
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}
