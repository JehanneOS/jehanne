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

int
POSIX_pause(int *errnop)
{
	rendezvous((void*)~0, 1);
	*errnop = __libposix_get_errno(PosixEINTR);
	return -1;
}

clock_t
POSIX_times(int *errnop, void *buf)
{
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}

char*
POSIX_getlogin(int *errnop)
{
	return jehanne_getuser();
}

int
POSIX_getlogin_r(int *errnop, char *name, int namesize)
{
	static char user[64];
	int fd;
	int n;

	fd = open("/dev/user", OREAD);
	if(fd < 0)
		goto None;
	n = read(fd, user, (sizeof user)-1);
	close(fd);
	if(n <= 0)
		goto None;
	if(namesize < n){
		*errnop = __libposix_get_errno(PosixERANGE);
		return __libposix_get_errno(PosixERANGE);
	}
	user[n] = 0;
	return 0;
None:
	if(namesize < 5){
		*errnop = __libposix_get_errno(PosixERANGE);
		return __libposix_get_errno(PosixERANGE);
	}
	jehanne_strcpy(name, "none");
	name[5] = 0;
	return 0;
}

char*
POSIX_getcwd(int *errnop, char *buf, int size)
{
	long len;
	if(buf == nil || size <= 0){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return nil;
	}
	len = jehanne_getwd(buf, size);
	if(len == 0){
		*errnop = __libposix_get_errno(PosixEACCES);
		return nil;
	}
	if(len < 0){
		*errnop = __libposix_get_errno(PosixERANGE);
		return nil;
	}
	return buf;
}

char*
POSIX_getpass(int *errnop, const char *prompt)
{
	int r, w, ctl;
	char *p;
	static char buf[256];

	if(fd2path(0, buf, sizeof(buf)) == 0 && strcmp("/dev/cons", buf) == 0)
		r = 0;
	else if((r = open("/dev/cons", OREAD)) < 0)
		goto ReturnENXIO;

	if(fd2path(1, buf, sizeof(buf)) == 0 && strcmp("/dev/cons", buf) == 0)
		w = 1;
	else if((w = open("/dev/cons", OWRITE)) < 0)
		goto CloseRAndReturnENXIO;

	if((ctl = open("/dev/consctl", OWRITE)) < 0)
		goto CloseRWAndReturnENXIO;

	fprint(w, "%s", prompt);

	write(ctl, "rawon", 5);
	p = buf;
	while(p < buf+sizeof(buf)-1 && read(r, p, 1) == 1){
		if(*p == '\n')
			break;
		if(*p == ('u' & 037))
			p = buf;
		else if(*p == '\b'){
			if (p > buf)
				--p;
		} else
			++p;
	}
	*p = '\0';
	write(ctl, "rawoff", 6);

	return buf;

CloseRWAndReturnENXIO:
	close(w);
CloseRAndReturnENXIO:
	close(r);
ReturnENXIO:
	*errnop = __libposix_get_errno(PosixENXIO);
	return nil;
}
