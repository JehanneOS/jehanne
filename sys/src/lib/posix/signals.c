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

#define __POSIX_SIGNAL_PREFIX_LEN (sizeof(__POSIX_SIGNAL_PREFIX)-1)

static PosixSignalTrampoline __libposix_signal_trampoline;

static int
note_all_writable_processes(int *errnop, char *note)
{
	// TODO: loop over writable note files and post note.
	*errnop = __libposix_get_errno(PosixEPERM);
	return -1;
}

int
POSIX_kill(int *errnop, int pid, int sig)
{
	char msg[64], file[128];
	int mode;
	int ret;

	snprint(msg, sizeof(msg), __POSIX_SIGNAL_PREFIX "%d", sig);
	switch(pid){
	case 0:
		mode = PNGROUP;
		break;
	case -1:
		return note_all_writable_processes(errnop, msg);
	default:
		if(pid < 0){
			mode = PNGROUP;
			pid = -pid;
		} else {
			mode = PNPROC;
		}
	}

	snprint(file, sizeof(file), "/proc/%d/note", pid);
	if(access(file, AWRITE) != 0){
		if(access(file, AEXIST) == 0)
			*errnop = __libposix_get_errno(PosixEPERM);
		else
			*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	ret = postnote(mode, pid, msg);
	if(ret != 0){
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_kill);
		return -1;
	}
	return 0;
}

static int
translate_jehanne_note(char *note)
{
	// TODO: implement
	return 0;
}

static void
terminated_by_signal(int sig)
{
	char buf[64];

	__libposix_free_wait_list();

	snprint(buf, sizeof(buf), __POSIX_EXIT_SIGNAL_PREFIX "%d", sig);
	exits(buf);
}

static int
send_control_msg(int pid, char *msg)
{
	int fd, n;
	char buf[256];

	n = snprint(buf, sizeof(buf), "/proc/%d/ctl", pid);
	if(n < 0)
		goto ErrorBeforeOpen;
	fd = open(buf, OWRITE);
	if(fd < 0)
		goto ErrorBeforeOpen;
	n = snprint(buf, sizeof(buf), "%s", msg);
	if(n < 0)
		goto ErrorAfterOpen;
	if(write(fd, buf, n) < n)
		goto ErrorAfterOpen;
	close(fd);
	return 1;

ErrorAfterOpen:
	close(fd);
ErrorBeforeOpen:
	return 0;
}

int
POSIX_signal_execute(int sig, PosixSignalDisposition action, int pid)
{
	switch(action){
	case SignalHandled:
		return 1;
	case TerminateTheProcess:
	case TerminateTheProcessAndCoreDump:
		terminated_by_signal(sig);
		break;
	case StopTheProcess:
		if(pid < 0)
			sysfatal("libposix: signal %d with stop disposition reached process %d", sig, pid);
		return send_control_msg(pid, "stop");
	case ResumeTheProcess:
		if(pid < 0)
			sysfatal("libposix: signal %d with continue disposition reached process %d", sig, pid);
		return send_control_msg(pid, "start");
	}
	return 0;
}

int
__libposix_note_handler(void *ureg, char *note)
{
	int sig;
	PosixSignalDisposition action;
	if(strncmp(note, __POSIX_SIGNAL_PREFIX, __POSIX_SIGNAL_PREFIX_LEN) != 0)
		return translate_jehanne_note(note); // TODO: should we translate common notes?
	sig = atoi(note+__POSIX_SIGNAL_PREFIX_LEN);
	action = __libposix_signal_trampoline(sig);
	return POSIX_signal_execute(sig, action, -1);
}

int
libposix_set_signal_trampoline(PosixSignalTrampoline trampoline)
{
	if(__libposix_initialized())
		return 0;
	if(trampoline == nil)
		return 0;
	__libposix_signal_trampoline = trampoline;
	return 1;
}

void
__libposix_signal_check_conf(void)
{
	if(__libposix_signal_trampoline == nil)
		sysfatal("libposix: no signal trampoline");
}
