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

static int
fcntl_dup(int *errnop, int fd, int minfd)
{
	int newfd, i;
	char buf[128];
	int opened[32];
	PosixError e;

	if(fd < 0 || minfd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}

	snprint(buf, sizeof(buf), "/fd/%d", minfd);
	if(access(buf, AEXIST) != 0){
		if(dup(fd, minfd) < 0){
			*errnop = __libposix_get_errno(PosixEBADF);
			return -1;
		}
		return minfd;
	}

	e = 0;
	i = 0;
	do
	{
		newfd = dup(fd, -1);
		if(newfd < 0){
			e = PosixEINVAL;
			break;
		}
		opened[i++] = newfd;
	} while(newfd < minfd && i < nelem(opened));

	--i;
	if(newfd >= minfd)
		--i;
	while(i >= 0)
		close(opened[i--]);
	if(newfd >= minfd)
		return newfd;

	close(newfd);
	if(e == 0)
		e = PosixEMFILE;
	*errnop = __libposix_get_errno(e);
	return -1;
}

static int
file_flags(int *errnop, int fd)
{
	int newfd, r, flags;
	char buf[128], *cols[3];

	snprint(buf, sizeof(buf), "/fd/%dctl", fd);
	newfd = open(buf, OREAD);
	if(newfd < 0)
		goto FailWithEBADF;
	r = read(newfd, buf, sizeof(buf));
	close(newfd);
	if(r < 0)
		goto FailWithEBADF;

	r = getfields(buf, cols, 3, 1, " ");
	if(r < 3)
		goto FailWithEBADF;

	flags = 0;

	if(strchr(cols[1], 'E'))
		flags |= OCEXEC;
	else if(__libposix_should_close_on_exec(fd))
		flags |= OCEXEC;

	if(strchr(cols[1], 'r'))
		flags |= OREAD;
	else if(strchr(cols[1], 'R'))
		flags |= OREAD;

	if(strchr(cols[1], 'w'))
		flags |= OWRITE;
	else if(strchr(cols[1], 'W'))
		flags |= OWRITE;

	return flags;

FailWithEBADF:
	*errnop = __libposix_get_errno(PosixEBADF);
	return -1;
}

int
POSIX_fcntl(int *errnop, int fd, PosixFDCmds cmd, uintptr_t arg)
{
	int tmp;
	int flags;
	switch(cmd){
	case PosixFDCDupFD:
		return fcntl_dup(errnop, fd, (int)arg);
	case PosixFDCDupFDCloseOnExec:
		if((tmp = fcntl_dup(errnop, fd, (int)arg)) < 0)
			return -1;
		flags = file_flags(errnop, tmp);
		if(flags < 0)
			return -1;
		if((flags&OCEXEC) == 0)
			__libposix_set_close_on_exec(tmp, 1);
		return tmp;
	case PosixFDCGetFD:
		flags = file_flags(errnop, fd);
		if(flags < 0)
			return -1;
		return flags & OCEXEC;
	case PosixFDCSetFD:
		if(arg){
			if((file_flags(errnop, fd)&OCEXEC) == 0)
				__libposix_set_close_on_exec(fd, 1);
		} else {
			if((file_flags(errnop, fd)&OCEXEC) != 0)
				__libposix_set_close_on_exec(fd, 0);
		}
		return 0;
	case PosixFDCGetFL:
		flags = file_flags(errnop, fd);
		if(flags < 0)
			return -1;
		return flags & (~OCEXEC);
	case PosixFDCSetFL:
		return 0;
		break;
	}

	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}
