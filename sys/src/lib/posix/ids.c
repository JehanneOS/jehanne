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
#include <9P2000.h>
#include <posix.h>
#include "internal.h"

int __libposix_session_leader = -1;

static int
get_noteid(int *errnop, int pid)
{
	int n, f;
	char buf[30];

	sprint(buf, "/proc/%d/noteid", pid);
	f = open(buf, 0);
	if(f < 0){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	n = read(f, buf, sizeof(buf) - 1);
	if(n < 0){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	buf[n] = '\0';
	n = atoi(buf);
	close(f);

	return n;
}

static int
set_noteid(int *errnop, int pid, int noteid)
{
	int n, f;
	char buf[30];

	if(pid == 0)
		pid = getpid();
	if(noteid == 0){
		noteid = get_noteid(errnop, pid);
		if(noteid < 0)
			return noteid;
	}
	sprint(buf, "/proc/%d/noteid", pid);
	f = open(buf, 1);
	if(f < 0) {
		*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	n = sprint(buf, "%d", noteid);
	n = write(f, buf, n);
	if(n < 0){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	close(f);
	return 0;
}

int
POSIX_getuid(int *errnop)
{
	return 0;
}

int
POSIX_geteuid(int *errnop)
{
	return 0;
}

int
POSIX_setuid(int *errnop, int uid)
{
	return 0;
}

int
POSIX_seteuid(int *errnop, int euid)
{
	return 0;
}

int
POSIX_setreuid(int *errnop, int ruid, int euid)
{
	return 0;
}

int
POSIX_getgid(int *errnop)
{
	return 0;
}

int
POSIX_getegid(int *errnop)
{
	return 0;
}

int
POSIX_setgid(int *errnop, int gid)
{
	return 0;
}

int
POSIX_setegid(int *errnop, int egid)
{
	return 0;
}

int
POSIX_setregid(int *errnop, int rgid, int egid)
{
	return 0;
}

int
POSIX_getpgrp(int *errnop)
{
	int pid = getpid();
	return get_noteid(errnop, pid);
}

int
POSIX_getpgid(int *errnop, int pid)
{
	return get_noteid(errnop, pid);
}

int
POSIX_setpgid(int *errnop, int pid, int pgid)
{
	if(pid < 0 || pgid < 0){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	return set_noteid(errnop, pid, pgid);
}

int
POSIX_getsid(int *errnop, int pid)
{
	int reqnoteid, mynoteid;

	if(pid < 0){
		*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	if(pid == 0)
		pid = getpid();
	else if(pid == getpid())
		return __libposix_session_leader;
	reqnoteid = get_noteid(errnop, pid);
	if(reqnoteid < 0)
		return reqnoteid;
	if(__libposix_session_leader < 0)
		return reqnoteid;
	mynoteid = POSIX_getpgrp(errnop);
	if(mynoteid == reqnoteid){
		/* if it share our pgrp (aka noteid), it shares
		 * our session leader
		 */
		return __libposix_session_leader;
	}
	return reqnoteid;
}

int
POSIX_setsid(int *errnop)
{
	if(rfork(RFNAMEG|RFNOTEG) < 0){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	__libposix_session_leader = POSIX_getpgrp(errnop);
	return __libposix_session_leader;
}
