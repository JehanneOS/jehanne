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

unsigned char *__signals_to_code_map;
unsigned char *__code_to_signal_map;
int *__handling_external_signal;

static int __sigrtmin;
static int __sigrtmax;
static int __min_known_sig;
static int __max_known_sig;
static PosixSignalTrampoline __libposix_signal_trampoline;

typedef enum PosixSignalDisposition
{
	SignalHandled = 0,	/* the application handled the signal */
	TerminateTheProcess,
	TerminateTheProcessAndCoreDump,
	StopTheProcess,
	ResumeTheProcess
} PosixSignalDisposition;

static int
note_all_writable_processes(int *errnop, char *note)
{
	// TODO: loop over writable note files and post note.
	*errnop = __libposix_get_errno(PosixEPERM);
	return -1;
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

/* Executes a PosixSignalDisposition for pid.
 *
 * MUST be called by POSIX_kill for unblockable signals.
 */
static int
execute_disposition(int sig, PosixSignalDisposition action, int pid)
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

static int
send_signal(int *errnop, int pid, int signal)
{
	char msg[64], file[128];
	int mode;
	int ret;

	snprint(msg, sizeof(msg), __POSIX_SIGNAL_PREFIX "%d", signal);
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


static PosixSignalDisposition
default_signal_disposition(int code)
{
	// see http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html
	if(code >= __sigrtmin || code <= __sigrtmin)
		return TerminateTheProcess;

	switch(__code_to_signal_map[code]){
	default:
		sysfatal("libposix: undefined signal %d", code);

	case PosixSIGALRM:
	case PosixSIGHUP:
	case PosixSIGINT:
	case PosixSIGKILL:
	case PosixSIGPIPE:
	case PosixSIGTERM:
	case PosixSIGUSR1:
	case PosixSIGUSR2:
	case PosixSIGPOLL:
	case PosixSIGPROF:
	case PosixSIGVTALRM:
		return TerminateTheProcess;
	case PosixSIGSTOP:
	case PosixSIGTSTP:
	case PosixSIGTTIN:
	case PosixSIGTTOU:
		return StopTheProcess;
	case PosixSIGABRT:
	case PosixSIGILL:
	case PosixSIGFPE:
	case PosixSIGBUS:
	case PosixSIGQUIT:
	case PosixSIGSEGV:
	case PosixSIGSYS:
	case PosixSIGTRAP:
	case PosixSIGXCPU:
	case PosixSIGXFSZ:
		return TerminateTheProcessAndCoreDump;
	case PosixSIGCHLD:
	case PosixSIGURG:
		return SignalHandled;
	case PosixSIGCONT:
		return ResumeTheProcess;
	}
}

int
POSIX_kill(int *errnop, int pid, int sig)
{
	PosixSignals signal;
	PosixSignalDisposition action;

	signal = __code_to_signal_map[sig];
	if(signal == 0
	&&(sig < __sigrtmin || sig > __sigrtmax))
		sysfatal("libposix: undefined signal %d", sig);
	if(pid == getpid()){
		if(__libposix_signal_trampoline(sig))
			action = SignalHandled;
		else
			action = default_signal_disposition(sig);
		if(!execute_disposition(sig, action, -1)){
			*errnop = __libposix_get_errno(PosixEPERM);
			return -1;
		}
	}

	switch(signal){
	case PosixSIGKILL:
		if(pid > 0)
		if(!send_control_msg(pid, "kill")){
			*errnop = __libposix_get_errno(PosixEPERM);
			return -1;
		}
		break;
	case PosixSIGSTOP:
		if(pid > 0)
		if(!send_control_msg(pid, "stop")){
			*errnop = __libposix_get_errno(PosixEPERM);
			return -1;
		}
		break;
	case PosixSIGCONT:
		if(pid > 0)
		if(!send_control_msg(pid, "start")){
			*errnop = __libposix_get_errno(PosixEPERM);
			return -1;
		}
		break;
	default:
		break;
	}
	return send_signal(errnop, pid, sig);
}

static int
translate_jehanne_note(char *note)
{
	// TODO: implement
	return 0;
}


int
__libposix_note_handler(void *ureg, char *note)
{
	int sig, ret;
	PosixSignalDisposition action;
	if(strncmp(note, __POSIX_SIGNAL_PREFIX, __POSIX_SIGNAL_PREFIX_LEN) != 0)
		return translate_jehanne_note(note); // TODO: should we translate common notes?
	sig = atoi(note+__POSIX_SIGNAL_PREFIX_LEN);
	if(sig < __min_known_sig || sig > __max_known_sig)
		sysfatal("libposix: '%s' does not carry a signal", note);
	*__handling_external_signal = 1;
	if(__libposix_signal_trampoline(sig))
		action = SignalHandled;
	else
		action = default_signal_disposition(sig);
	ret = execute_disposition(sig, action, -1);
	*__handling_external_signal = 0;
	return ret;
}

int
libposix_define_realtime_signals(int sigrtmin, int sigrtmax)
{
	if(sigrtmin >= 256 || sigrtmin <=0)
		sysfatal("libposix: invalid SIGRTMIN %d (must be positive and less then 256)", sigrtmin);
	if(sigrtmax >= 256 || sigrtmax <=0)
		sysfatal("libposix: invalid SIGRTMAX %d (must be positive and less then 256)", sigrtmax);
	if(sigrtmax <= sigrtmin)
		sysfatal("libposix: invalid SIGRTMAX %d (must be greater than SIGRTMIN %d)", sigrtmax, sigrtmin);
	if(__libposix_initialized())
		return 0;
	__sigrtmin = sigrtmin;
	__sigrtmax = sigrtmax;
	if(sigrtmin < __min_known_sig || __min_known_sig == 0)
		__min_known_sig = sigrtmin;
	if(sigrtmax > __max_known_sig || __max_known_sig == 0)
		__max_known_sig = sigrtmax;
	return 1;
}

int
libposix_define_signal(PosixSignals signal, int code)
{
	if(signal >= PosixNumberOfSignals)
		sysfatal("libposix: unknown PosixSignal %d", signal);
	if(code >= 256 || code <=0)
		sysfatal("libposix: invalid signal number %d (must be positive and less then 256)", code);
	if(__libposix_initialized())
		return 0;
	__signals_to_code_map[signal] = (unsigned char)code;
	__code_to_signal_map[code] = (unsigned char)signal;
	if(code < __min_known_sig || __min_known_sig == 0)
		__min_known_sig = code;
	if(code > __max_known_sig || __max_known_sig == 0)
		__max_known_sig = code;
	return 1;
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
