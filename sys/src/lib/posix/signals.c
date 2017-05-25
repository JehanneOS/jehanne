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
 * We distinguish control signals (SIGKILL, SIGSTOP, SIGCONT,
 * SIGABRT, SIGIOT), SIGCHLD/SIGCLD, timers' wakeups
 * (SIGALRM, SIGPROF, SIGVTALRM) and all the others.
 *
 * TRASMISSION
 * -----------
 * Signal transmission depends on the relation between the sender
 * and the receiver:
 * 1) if sender and receiver have no relation the signal is translated
 *    to a note and sent to the receiver(s);
 * 2) if sender == receiver the signal trampoline is directly invoked
 *    for all signals except control ones and the default disposition occurs if
 *    the trampoline does not handle the signal
 * 3) if sender is parent or child of receiver the transmision
 *    differs from the default if libposix_emulate_SIGCHLD() has been
 *    called during library initialization
 *
 * Since notes in Jehanne are not reentrant, signals translated to
 * notes will be enqueued in kernel. A special machinery is implemented
 * for timers, so that they can be used in signal handlers.
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
 * - SIGSTOP => write "stop" to the control file of the current process
 * - SIGABRT/SIGIOT => invoke the registered signal handlers and abort
 *                     the current process
 * - SIGCONT => invoke the registered signal handlers via the signal
 *              trampoline and continue; note that (since a stopped
 *              process cannot resume itself) the sender will write
 *              "start" to the control file of the receiver before
 *              sending the note (unless SIGCHLD emulation is enable).
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
 * Each fork() will spawn two additional processes that are designed
 * to proxy signals between the parent and the desired child:
 *
 *   parent
 *     +- proxy from parent to child (P2C)
 *          +- proxy from child to parent (C2P)
 *               +- child
 *
 * Fork will return the pid of P2C to the parent, so that the
 * parent process will see the first proxy as its child; the child will
 * see the second as its parent. Each proxy waits for its child and
 * forwards the notes it receive to its designed target.
 * Finally fork in child will return 0.
 *
 * When the child exits, C2P will send a SIGCHLD note to parent and
 * then exit with the same status. Then P2C will exit with
 * the same status too.
 *
 * As the parent process will see P2C as its child, it will send any
 * signal to it via kill and will wait it on SIGCHLD.
 * However, when this machinary is enabled, kill will treat all signals
 * for forked children in the same way, sending them to P2C.
 * It's P2C's responsibility to translate control messages as required,
 * so that SIGCONT will work as expected.
 *
 * TIMERS
 * ------
 * The functions alarm() and settimer() generate SIGALRM, SIGPROF
 * or SIGVTALRM for the current process. We want timers to be able to
 * expire in a signal handler (interrupting a blocking syscall) but
 * without giving up the simplicity of notes.
 *
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

unsigned char *__signals_to_code_map;
unsigned char *__code_to_signal_map;
int *__handling_external_signal;

static int __sigrtmin;
static int __sigrtmax;
int __min_known_sig;
int __max_known_sig;
static PosixSignalTrampoline __libposix_signal_trampoline;

typedef enum PosixSignalDisposition
{
	SignalHandled = 0,	/* the application handled the signal */
	TerminateTheProcess,
	TerminateTheProcessAndCoreDump,
	StopTheProcess,
	ResumeTheProcess
} PosixSignalDisposition;

static PosixError
note_all_writable_processes(int sig)
{
	// TODO: loop over writable note files and post note.
	return PosixEPERM;
}

static void
terminated_by_signal(int sig)
{
	char buf[64];

	__libposix_free_wait_list();

	snprint(buf, sizeof(buf), __POSIX_EXIT_SIGNAL_PREFIX "%d", sig);
	exits(buf);
}

int
__libposix_send_control_msg(int pid, char *msg)
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

/* Executes a PosixSignalDisposition.
 */
static int
execute_disposition(int sig, PosixSignalDisposition action)
{
	switch(action){
	case ResumeTheProcess:	// the sender resumed us already
	case SignalHandled:
		return 1;
	case TerminateTheProcess:
	case TerminateTheProcessAndCoreDump:
		terminated_by_signal(sig);
		break;
	case StopTheProcess:
		return __libposix_send_control_msg(getpid(), "stop");
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

PosixError
__libposix_receive_signal(int sig)
{
	PosixSignalDisposition action;
	PosixSignals psig = __code_to_signal_map[sig];
	
	if(psig != PosixSIGKILL && psig != PosixSIGSTOP
	&& __libposix_signal_trampoline(sig))
		action = SignalHandled;
	else
		action = default_signal_disposition(sig);
	if(!execute_disposition(sig, action))
		return PosixEPERM;
	return 0;
}

PosixError
__libposix_notify_signal_to_process(int pid, int signal)
{
	char buf[128];
	int fd, n;

	snprint(buf, sizeof(buf), "/proc/%d/note", pid);
	if((fd = open(buf, OWRITE)) < 0){
		if(access(buf, AEXIST) == 0)
			return PosixEPERM;
		else
			return PosixESRCH;
	}
	n = snprint(buf, sizeof(buf), __POSIX_SIGNAL_PREFIX "%d", signal);
	write(fd, buf, n);
	close(fd);
	return 0;
}

static PosixError
notify_signal_to_group(int pid, int signal)
{
	char buf[128];
	int fd, n;

	snprint(buf, sizeof(buf), "/proc/%d/notepg", pid);
	if((fd = open(buf, OWRITE)) < 0){
		if(access(buf, AEXIST) == 0)
			return PosixEPERM;
		else
			return PosixESRCH;
	}
	n = snprint(buf, sizeof(buf), __POSIX_SIGNAL_PREFIX "%d", signal);
	write(fd, buf, n);
	close(fd);
	return 0;
}

static PosixError
dispatch_signal(int pid, int sig)
{
	PosixSignals signal;
	PosixError error;
	switch(pid){
	case 0:
		return notify_signal_to_group(getpid(), sig);
	case -1:
		return note_all_writable_processes(sig);
	default:
		if(pid < 0)
			return notify_signal_to_group(-pid, sig);
		break;
	}
	signal = __code_to_signal_map[sig];
	error = __libposix_notify_signal_to_process(pid, sig);
	if(signal == PosixSIGCONT && !__libposix_is_child(pid))
		__libposix_send_control_msg(pid, "start");
	return error;
}

int
POSIX_kill(int *errnop, int pid, int sig)
{
	PosixSignals signal;
	PosixError perror;

	signal = __code_to_signal_map[sig];
	if(signal == 0
	&&(sig < __sigrtmin || sig > __sigrtmax))
		sysfatal("libposix: undefined signal %d", sig);
	if(pid == getpid())
		perror = __libposix_receive_signal(sig);
	else
		perror = dispatch_signal(pid, sig);
	if(perror != 0){
		*errnop = __libposix_get_errno(perror);
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

int
__libposix_note_to_signal(char *note)
{
	return atoi(note+__POSIX_SIGNAL_PREFIX_LEN);
}

int
__libposix_note_handler(void *ureg, char *note)
{
	int sig;
	PosixError error;
	if(strncmp(note, __POSIX_SIGNAL_PREFIX, __POSIX_SIGNAL_PREFIX_LEN) != 0)
		return translate_jehanne_note(note); // TODO: should we translate common notes?
	sig = __libposix_note_to_signal(note);
	if(sig < __min_known_sig || sig > __max_known_sig)
		sysfatal("libposix: '%s' does not carry a signal", note);
	*__handling_external_signal = 1;
	error = __libposix_receive_signal(sig);
	*__handling_external_signal = 0;
	return error == 0;
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
