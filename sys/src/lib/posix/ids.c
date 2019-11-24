/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017-2019 Giacomo Tesio <giacomo@tesio.it>
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

typedef union {
	int	group;
	char	raw[sizeof(int)];
} IntBuf;

extern int *__libposix_devsignal;
int __libposix_session_leader = -1;

static int
get_ppid(int pid)
{
	long n;
	char buf[32];
	sprint(buf, "/proc/%d/ppid", pid);
	n = remove(buf);
	if(n == -1)
		return -1;
	return (int)n;
}

long
__libposix_sighelper_set_pgid(int target, int group)
{
	union {
		PosixHelperRequest	request;
		long			raw;
	} offset;
	IntBuf buf;
	long ret;

	offset.request.command = PHSetProcessGroup;
	offset.request.target = target;

	buf.group = group;
	ret = pwrite(*__libposix_devsignal, buf.raw, sizeof(buf.raw), offset.raw);
	return ret;
}

static int
set_group_id(int *errnop, int pid, int group)
{
	int mypid, ppid;

	if(pid < 0 || group < 0){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	if(pid == __libposix_session_leader){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	mypid = *__libposix_pid;
	if(pid == 0 && group == 0){
		/* the caller wants a new process group */
CreateNewProcessGroup:
		rfork(RFNOTEG);
		return __libposix_sighelper_cmd(PHSetProcessGroup, mypid);
	}
	if(pid == 0)
		pid = mypid;
	ppid = get_ppid(pid);
	if(ppid == -1 || pid != mypid && mypid != ppid){
		*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	if(group == 0)
		group = pid;
	if(pid == group && pid == mypid)
		goto CreateNewProcessGroup;

	return __libposix_sighelper_set_pgid(pid, group);
}

int
POSIX_getuid(int *errnop)
{
	return 1000;
}

int
POSIX_geteuid(int *errnop)
{
	return 1000;
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
	return 1000;
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
	long ret;
	int pid = *__libposix_pid;
	ret = __libposix_sighelper_cmd(PHGetProcessGroupId, pid);
	if(ret < 0)
		return pid;
	return ret;
}

int
POSIX_getpgid(int *errnop, int pid)
{
	long ret;
	ret = __libposix_sighelper_cmd(PHGetProcessGroupId, pid);
	if(ret < 0)
		*errnop = __libposix_get_errno(PosixESRCH);
	return ret;
}

int
POSIX_setpgid(int *errnop, int pid, int pgid)
{
	return set_group_id(errnop, pid, pgid);
}

int
POSIX_getsid(int *errnop, int pid)
{
	int mypid;
	long sid;
	char buf[32];

	if(pid < 0){
FailWithESRCH:
		*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	snprint(buf, sizeof(buf), "/proc/%d/ns", pid);
	if(access(buf, AEXIST) != 0)
		goto FailWithESRCH;

	mypid = *__libposix_pid;
	if(pid == 0)
		pid = mypid;
	sid = __libposix_sighelper_cmd(PHGetSessionId, pid);
	if(sid < 0){
		*errnop = __libposix_get_errno(PosixEPERM);
		return -1;
	}
	if(pid == mypid)
		__libposix_session_leader = (int)sid;

	return sid;
}

int
POSIX_setsid(int *errnop)
{
	extern PosixSignalMask *__libposix_signal_mask;
	extern SignalConf *__libposix_signals;
	extern int *__libposix_devsignal;

	char *posixly_args[4], *fname;
	int mypid = *__libposix_pid;
	int oldsid;
	long controlpid;
	SignalConf *c;

	/* detach the previous session */
	oldsid = (int)__libposix_sighelper_cmd(PHGetSessionId, 0);
	fname = smprint("#s/posixly.%s.%d", getuser(), oldsid);
	if(fname == nil || __libposix_sighelper_cmd(PHDetachSession, 0) < 0)
		goto FailWithEPERM;
	if(*__libposix_devsignal >= 0)
		close(*__libposix_devsignal);
	*__libposix_devsignal = -1;
	rfork(RFNAMEG|RFNOTEG|RFENVG|RFFDG);
	unmount(fname, "/dev");
	free(fname);
	fname = nil;
	assert(access("/dev/posix", AEXIST) != 0);

	/* start the new session */
	switch(controlpid = rfork(RFPROC|RFNOTEG|RFENVG|RFFDG)){
	case -1:
		goto FailWithEPERM;
	case 0:
		posixly_args[0] = "posixly";
		posixly_args[1] = "-p";
		posixly_args[2] = smprint("%d", mypid);
		posixly_args[3] = nil;
		jehanne_pexec("sys/posixly", posixly_args);
		rfork(RFNOWAIT);
		sysfatal("pexec sys/posixly");
	default:
		break;
	}

	/* wait for /dev/posix/ */
	fname = smprint("/proc/%d/note", controlpid);
	if(fname == nil)
		goto FailWithEPERM;
	while(access("/dev/posix", AEXIST) != 0 && access(fname, AWRITE) == 0)
		sleep(250);
	if(access(fname, AWRITE) != 0)
		goto FailWithEPERM;
	free(fname);
	fname = nil;

	/* connect to the new session */
	__libposix_sighelper_open();
	__libposix_sighelper_set(PHBlockSignals, *__libposix_signal_mask);
	c = __libposix_signals;
	while(c < __libposix_signals + PosixNumberOfSignals){
		if(c->handler == (void*)1)
			__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(1+(c-__libposix_signals)));
		++c;
	}
	__libposix_session_leader = mypid;
	return mypid;

FailWithEPERM:
	if(fname)
		free(fname);
	*errnop = __libposix_get_errno(PosixEPERM);
	return -1;
}
