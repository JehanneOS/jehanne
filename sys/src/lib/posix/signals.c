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

/* POSIX signals have weird semantics that are hard to emulate in a
 * sane system without giving up on its sanity.
 *
 * We distinguish control signals (SIGKILL, SIGSTOP, SIGTSTP, SIGCONT,
 * SIGABRT, SIGTTIN, SIGTTOU, SIGIOT), SIGCHLD/SIGCLD, timers' wakeups
 * (SIGALRM, SIGPROF, SIGVTALRM) and all the others.
 *
 * TRASMISSION
 * -----------
 * Signal transmission is done though a file server mounted /dev/posix/,
 * provided by sys/posixly. On startup and at each fork, processes
 * create a file named /dev/posix/signal with ORDWR mode and perm
 * equals to their pid. Writing and Reading such file they can
 * control signal dispatching, process groups and so on.
 *
 * When a signal is written to /dev/posix/signal, it is translated for
 * each receiver to a note, and written to the specific note file.
 *
 * If the receiver is a known process, the note is in the format
 * 	posix: si_signo si_pid si_uid si_value si_code
 * otherwise, if possible, it is translated to an idiomatic note
 * (eg "interrupt" or "alarm").
 *
 * Since notes in Jehanne are not reentrant, signals translated to
 * notes will be enqueued in kernel. A special machinery is implemented
 * for timers, so that they can be used in signal handlers to wakeup
 * the calling process.
 *
 * For all the signals except SIGCONT, the burden of interpreting the
 * signal is on the receiver: the sender just send the signal.
 *
 * The receiver will translate the note back to the appropriate signal
 * number and invoke the trampoline: if trampoline returns 0 no function
 * registered with signal() handled the signal and the library will 
 * invoke the default disposition associated to the signal.
 *
 * CONTROL MESSAGES
 * ----------------
 * Control messages are translated by the receiver to equivalent actions:
 * - SIGKILL => write "kill" to the control file of the current process
 * - SIGSTOP, SIGTSTP, SIGTTOU, SIGTTIN
 *            => write "stop" to the control file of the current process
 *               (when the signal is not handled nor ignored)
 * - SIGABRT/SIGIOT => invoke the registered signal handlers and abort
 *                     the current process
 * - SIGCONT => invoke the registered signal handlers via the signal
 *              trampoline and continue; note that (since a stopped
 *              process cannot resume itself) sys/posixly will write
 *              "start" to the control file of the receiver.
 *
 * SIGCHLD/SIGCLD
 * --------------
 * Jehanne (like Plan 9) does not support a SIGCHLD equivalent.
 * The user space emulation provided here is quite expensive, so it's
 * disabled by default.
 * Calling libposix_emulate_SIGCHLD() during libposix initialization
 * will enable this machinery for the whole life of the process.
 *
 * Such emulation changes the way POSIX_fork and POSIX_kill works.
 *
 * Each fork() will spawn an additional process that share the memory
 * of the parent, and waits for the child, so that it can send SIGCHLD:
 *
 *   parent
 *     +- nanny
 *          +- child
 *
 * Such "nannies" connect to sys/posixly by creating /dev/posix/nanny
 * so that the filesystem will handle them differently:
 * - any signal for the nanny sent from the parent is delivered to the child
 * - any signal for the nanny sent from the child is delivered to the parent
 * - any signal for the nanny sent from anybody else is delivered to the child
 *
 * Finally fork in child will return 0.
 *
 * When the child exits, the proxy will send a SIGCHLD sigchld to parent
 * and then exit with the same status.
 *
 * As the parent process will see the proxy as its child, it will send
 * any signal to it via kill or sigqueue and will wait it on SIGCHLD.
 *
 * TIMERS
 * ------
 * The functions sys_alarm() and setitimer() generate SIGALRM, SIGPROF
 * or SIGVTALRM for the current process. We want timers to be able to
 * expire in a signal handler (interrupting a blocking syscall) but
 * without giving up the simplicity of notes.
 *
 * (TO BE IMPLEMENTED: )
 * We allocate these timers on libposix initialization. When normal
 * code is running timers will be implemented via Jehanne's alarms,
 * producing a note on expiration that will be mapped to the proper
 * signal for the trampoline by the receiving process.
 *
 * However, when a signal handler is being executed, the timers will
 * be implemented using the awake syscall that will be able
 * to interrupt blocking syscalls in the signal handler.
 * When a signal handler returns, if the timer has not be cleared and
 * did not expired, the wakeup is cleared and replaced with an alarm
 * note.
 */

#include <u.h>
#include <lib9.h>
#include <posix.h>
#include "internal.h"

int *__handling_external_signal;
int *__restart_syscall;
extern PosixSignalMask *__libposix_signal_mask;
extern int *__libposix_devsignal;

typedef union {
	PosixSignalInfo	signal;
	char		raw[sizeof(PosixSignalInfo)];
} SignalBuf;
typedef union {
	PosixSignalMask	signals;
	char		raw[sizeof(PosixSignalMask)];
} SigSetBuf;

static SignalConf signal_configurations[PosixNumberOfSignals];
SignalConf *__libposix_signals = signal_configurations;

typedef enum PosixSignalDisposition
{
	IgnoreWithNoEffect = 0,
	TerminateTheProcess,
	TerminateTheProcessAndCoreDump,
	StopTheProcess,
	ResumeTheProcess
} PosixSignalDisposition;

static PosixError
note_all_writable_processes(PosixSignalInfo* siginfo)
{
	// TODO: loop over writable note files and post note.
	return PosixEPERM;
}

static void
terminated_by_signal(int signo)
{
	char buf[64];

	__libposix_free_wait_list();

	snprint(buf, sizeof(buf), __POSIX_EXIT_SIGNAL_PREFIX "%d", signo);
	exits(buf);
}

void
__libposix_init_signal_handlers(void)
{
	__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(PosixSIGCHLD));
	__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(PosixSIGURG));
	__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(PosixSIGWINCH));
	__libposix_signals[PosixSIGCHLD-1].handler = (void*)1;
	__libposix_signals[PosixSIGCHLD-1].sa_restart = 1;
	__libposix_signals[PosixSIGURG-1].handler = (void*)1;
	__libposix_signals[PosixSIGURG-1].sa_restart = 1;
	__libposix_signals[PosixSIGWINCH-1].handler = (void*)1;
	__libposix_signals[PosixSIGWINCH-1].sa_restart = 1;
}

int
__libposix_send_control_msg(int pid, char *msg)
{
	int fd, n;
	char buf[256];

	n = snprint(buf, sizeof(buf), "/proc/%d/ctl", pid);
	if(n < 0)
		goto ErrorBeforeOpen;
	fd = sys_open(buf, OWRITE);
	if(fd < 0)
		goto ErrorBeforeOpen;
	n = snprint(buf, sizeof(buf), "%s", msg);
	if(n < 0)
		goto ErrorAfterOpen;
	if(jehanne_write(fd, buf, n) < n)
		goto ErrorAfterOpen;
	sys_close(fd);
	return 1;

ErrorAfterOpen:
	sys_close(fd);
ErrorBeforeOpen:
	return 0;
}

/* Executes a PosixSignalDisposition.
 */
static int
execute_disposition(int signo, PosixSignalDisposition action)
{
	int aborted_by_signal;

	switch(action){
	case ResumeTheProcess:	// the sender resumed us already
	case IgnoreWithNoEffect:
		*__restart_syscall = 1;
		return 1;
	case TerminateTheProcess:
		terminated_by_signal(signo);
		break;
	case TerminateTheProcessAndCoreDump:
		aborted_by_signal = 0;
		assert(aborted_by_signal);
		break;
	case StopTheProcess:
		return __libposix_send_control_msg(*__libposix_pid, "stop");
	}
	return 0;
}

static PosixSignalDisposition
default_signal_disposition(PosixSignals signal)
{
	if(signal >= PosixSIGRTMIN && signal <= PosixSIGRTMAX)
		return TerminateTheProcess;

	switch(signal){
	default:
		sysfatal("libposix: undefined signal %d", signal);

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
		return IgnoreWithNoEffect;
	case PosixSIGCONT:
		return ResumeTheProcess;
	}
}

/* returns 1 if the signal handling has been completed, 0 otherwise */
int
__libposix_run_signal_handler(SignalConf *c, PosixSignalInfo *siginfo)
{
	PosixSigHandler h;
	PosixSigAction a;
	PosixSignalMask m;

	switch((uintptr_t)c->handler){
	case 0:
		/* SIG_DFL */
		if(c->sa_restart)
			*__restart_syscall = 1;
		break;
	case 1:
		/* SIG_IGN */
		if(siginfo->si_signo == PosixSIGABRT)
			break;
		*__restart_syscall = 1;
		return 1;
	default:
		m = *__libposix_signal_mask;
		*__libposix_signal_mask |= c->mask;
		__libposix_sighelper_set(PHBlockSignals, *__libposix_signal_mask);
		a = c->handler;
		h = c->handler;

		if(c->sa_resethand)
			c->handler = 0;

		if(c->sa_siginfo){
			a(siginfo->si_signo, siginfo, nil);
		} else {
			h(siginfo->si_signo);
		}
		if(c->sa_restart)
			*__restart_syscall = 1;
		*__libposix_signal_mask = m;
		__libposix_sighelper_set(PHBlockSignals, *__libposix_signal_mask);
		if(siginfo->si_signo == PosixSIGABRT)
			break;
		return 1;
	}
	return 0;
}

PosixError
__libposix_receive_signal(PosixSignalInfo *siginfo)
{
	SignalConf *c;
	PosixSignalDisposition disposition;
	PosixSignals signo = siginfo->si_signo;

	if(signo == PosixSIGKILL || signo == PosixSIGSTOP)
		goto ExecuteDefaultDisposition;

	if(__libposix_signal_blocked(siginfo))
		return 0;

	c = __libposix_signals + (signo-1);
	if(__libposix_run_signal_handler(c, siginfo))
		return 0;

ExecuteDefaultDisposition:
	disposition = default_signal_disposition(signo);
	if(!execute_disposition(signo, disposition))
		return PosixEPERM;
	return 0;
}

long
__libposix_sighelper_signal(PosixHelperCommand command, int posix_process_pid, PosixSignalInfo *siginfo)
{
	union {
		PosixHelperRequest	request;
		long			raw;
	} offset;
	char buf[sizeof(PosixSignalInfo)];

	offset.request.command = command;
	offset.request.target = posix_process_pid;

	memcpy(buf, siginfo, sizeof(buf));

	return sys_pwrite(*__libposix_devsignal, buf, sizeof(buf), offset.raw);
}

long
__libposix_sighelper_set(PosixHelperCommand command, PosixSignalMask signal_set)
{
	union {
		PosixHelperRequest	request;
		long			raw;
	} offset;
	union {
		PosixSignalMask	mask;
		char		raw[sizeof(PosixSignalMask)];
	} buffer;

	offset.request.command = command;
	offset.request.target = 0;

	buffer.mask = signal_set;

	return sys_pwrite(*__libposix_devsignal, buffer.raw, sizeof(buffer.raw), offset.raw);
}

PosixError
__libposix_notify_signal_to_process(int pid, PosixSignalInfo *siginfo)
{
	long e = __libposix_sighelper_signal(PHSignalProcess, pid, siginfo);
	return (PosixError)e;
}

static PosixError
notify_signal_to_group(int pid, PosixSignalInfo* siginfo)
{
	long e = __libposix_sighelper_signal(PHSignalGroup, pid, siginfo);
	return (PosixError)e;
}

PosixError
__libposix_dispatch_signal(int pid, PosixSignalInfo* siginfo)
{
	PosixError error;
	switch(pid){
	case 0:
		return notify_signal_to_group(*__libposix_pid, siginfo);
	case -1:
		return note_all_writable_processes(siginfo);
	default:
		if(pid < 0)
			return notify_signal_to_group(-pid, siginfo);
		break;
	}
	error = __libposix_notify_signal_to_process(pid, siginfo);
	if(siginfo->si_signo == PosixSIGCONT && !__libposix_is_child(pid))
		__libposix_send_control_msg(pid, "start");
	return error;
}

static int
translate_jehanne_kernel_note(const char *note, PosixSignalInfo *siginfo)
{
	char *trap[3];
	char *tmp;

	assert(siginfo->si_signo == 0);

	if(strncmp("trap: fault ", note, 12) == 0){
		// trap: fault read addr=0x0 pc=0x400269
		note += 12;
		siginfo->si_signo = PosixSIGTRAP;
		tmp = strdup(note);
		if(getfields(tmp, trap, 3, 1, " ") == 3){
			if(trap[0][0] == 'r')
				siginfo->si_code = PosixSIFaultMapError;
			else
				siginfo->si_code = PosixSIFaultAccessError;
			siginfo->si_value._sival_raw = atoll(trap[1]+5);
		}
		free(tmp);
	} else if(strncmp("write on closed pipe", note, 20) == 0){
		// write on closed pipe pc=0x400269
		siginfo->si_signo = PosixSIGPIPE;
		note += 24;
		siginfo->si_value._sival_raw = atoll(note);
	} else if(strncmp("bad address in syscall", note, 22) == 0){
		// bad address in syscall pc=0x41cc54
		siginfo->si_signo = PosixSIGSEGV;
		note += 26;
		siginfo->si_value._sival_raw = atoll(note);
	}
	// TODO: implement

	return siginfo->si_signo == 0 ? 0 : 1;
}

static int
translate_jehanne_note(const char *note, PosixSignalInfo *siginfo)
{
	if(note == nil || note[0] == 0)
		return 0;

	if(strncmp("alarm", note, 5) == 0){
		siginfo->si_signo = PosixSIGALRM;
		return 1;
	}
	if(strncmp("sys: ", note, 5) == 0)
		return translate_jehanne_kernel_note(note + 5, siginfo);
	if(strncmp("interrupt", note, 9) == 0){
		siginfo->si_signo = PosixSIGINT;
		return 1;
	}
	if(strncmp("hangup", note, 6) == 0){
		siginfo->si_signo = PosixSIGHUP;
		return 1;
	}

	return 0;
}

int
__libposix_signal_to_note(const PosixSignalInfo *si, char *buf, int size)
{
	return
	snprint(buf, size, __POSIX_SIGNAL_PREFIX "%d %d %d %#p %d",
		si->si_signo, si->si_pid, si->si_uid,
		si->si_value._sival_raw, si->si_code);
}

/* The format of a note sent by libposix as a signal is
 *
 *	"posix: %d %d %d %#p %d", signo, pid, uid, value, code
 */
int
__libposix_note_to_signal(const char *note, PosixSignalInfo *siginfo)
{
	assert(siginfo->si_signo == 0); /* siginfo must be zeroed */
	if(strncmp(note, __POSIX_SIGNAL_PREFIX, __POSIX_SIGNAL_PREFIX_LEN) != 0)
		return translate_jehanne_note(note, siginfo);
	char *rest = (char*)note + __POSIX_SIGNAL_PREFIX_LEN;
	if(*rest == 0)
		return 0;
	siginfo->si_signo = strtol(rest, &rest, 0);
	if(*rest == 0)
		return 1;
	siginfo->si_pid = strtol(rest, &rest, 0);
	if(*rest == 0)
		return 1;
	siginfo->si_uid = strtoul(rest, &rest, 0);
	if(*rest == 0)
		return 1;
	siginfo->si_value._sival_raw = strtoull(rest, &rest, 0);
	if(*rest == 0)
		return 1;
	siginfo->si_code = strtoul(rest, &rest, 0);
	return 1;
}

int
__libposix_note_handler(void *ureg, char *note)
{
	PosixSignalInfo siginfo;
	PosixError error;

	memset(&siginfo, 0, sizeof(PosixSignalInfo));

	if(!__libposix_note_to_signal(note, &siginfo)
	&& (siginfo.si_signo < 1 || siginfo.si_signo > PosixNumberOfSignals))
		sysfatal("libposix: '%s' does not carry a signal", note);
	*__handling_external_signal = 1;
	error = __libposix_receive_signal(&siginfo);
	*__handling_external_signal = 0;
	werrstr("interrupted");
	return error == 0;
}

int
__libposix_restart_syscall(void)
{
	int r;

	if(*__handling_external_signal)
		return 0;

	r = *__restart_syscall;
	*__restart_syscall = 0;
	return r;
}

int
POSIX_sigaction(int *errnop, int signo, const struct sigaction *act, struct sigaction *old)
{
	SignalConf *c, oconf;

	if(signo < 1 || signo > PosixNumberOfSignals)
		goto FailWithEINVAL;

	c = __libposix_signals + (signo-1);

	if(old)
		memcpy(&oconf, c, sizeof(SignalConf));

	if(act){
		memset(c, 0, sizeof(SignalConf));
		if(signo == PosixSIGKILL || signo == PosixSIGSTOP)
			goto FailWithEINVAL;
		if(act->sa_flags & PosixSAFSigInfo){
			c->sa_siginfo = 1;
			c->handler = act->sa_sigaction;
		} else {
			c->handler = act->sa_handler;
		}
		c->mask = act->sa_mask & ~((255UL<<56) | SIGNAL_MASK(PosixSIGKILL) | SIGNAL_MASK(PosixSIGSTOP));
		if(act->sa_flags & PosixSAFResetHandler)
			c->sa_resethand = 1;
		else
			c->sa_resethand = 0;
		if(act->sa_flags & PosixSAFRestart)
			c->sa_restart = 1;
		else
			c->sa_restart = 0;
		if(signo == PosixSIGCHLD){
			if(act->sa_flags & PosixSAFNoChildrenWait)
				c->sa_nochildwait = 1;
			else
				c->sa_nochildwait = 0;
			if(c->handler == (void*)1) /* SIGCHLD && SIG_IGN => SA_NOCLDWAIT */
				c->sa_nochildwait = 1;
		}
		if(c->handler == (void*)1){
			/* ignore signal */
			__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(signo));
		} else if(c->handler == (void*)0){
			/* default behavior */
			switch(signo){
			case PosixSIGCHLD:
			case PosixSIGURG:
			case PosixSIGWINCH:
				__libposix_sighelper_set(PHIgnoreSignal, SIGNAL_MASK(signo));
				break;
			default:
				__libposix_sighelper_set(PHEnableSignal, SIGNAL_MASK(signo));
				break;
			}
		} else {
			/* do not ignore signal */
			__libposix_sighelper_set(PHEnableSignal, SIGNAL_MASK(signo));
		}
	}

	if(old){
		if(oconf.sa_siginfo){
			old->sa_sigaction = oconf.handler;
			old->sa_mask = oconf.mask;
			old->sa_flags = 0;
			if(oconf.sa_siginfo)
				old->sa_flags |= PosixSAFSigInfo;
			if(oconf.sa_resethand)
				old->sa_flags |= PosixSAFResetHandler;
			if(oconf.sa_restart)
				old->sa_flags |= PosixSAFRestart;
			if(oconf.sa_nochildwait)
				old->sa_flags |= PosixSAFNoChildrenWait;
		} else {
			old->sa_handler = oconf.handler;
			old->sa_flags = 0;
			old->sa_mask = 0;
		}
	}
	return 0;

FailWithEINVAL:
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}
