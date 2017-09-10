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

#define __POSIX_SIGNAL_PREFIX_LEN (sizeof(__POSIX_SIGNAL_PREFIX)-1)

typedef struct WaitList WaitList;
struct WaitList
{
	int pid;
	int status;
	WaitList *next;
};

typedef struct ChildList ChildList;
struct ChildList
{
	int pid;
	ChildList *next;
};

typedef struct SignalConf SignalConf;
struct SignalConf
{
	void* handler;
	PosixSignalMask mask	: 56;
	int sa_siginfo		: 1;
	int sa_resethand	: 1;
	int sa_restart		: 1;
	int sa_nochildwait	: 1;
};

typedef struct PendingSignalList PendingSignalList;
struct PendingSignalList
{
	PosixSignalInfo		signal;
	PendingSignalList*	next;
};

extern int *__libposix_pid;

extern void __libposix_files_check_conf(void);
extern void __libposix_errors_check_conf(void);
extern void __libposix_processes_check_conf(void);
extern int __libposix_initialized(void);

extern int __libposix_get_errno(PosixError e);

extern void __libposix_setup_exec_environment(char * const *env);

extern int __libposix_translate_errstr(uintptr_t caller);

extern int __libposix_note_handler(void *ureg, char *note);

extern void __libposix_free_wait_list(void);

extern void __libposix_setup_new_process(void);

extern int __libposix_signal_to_note(const PosixSignalInfo *si, char *buf, int size);

extern int __libposix_note_to_signal(const char *note, PosixSignalInfo *siginfo);

extern int __libposix_is_child(int pid);

extern void __libposix_free_child_list(void);

extern void __libposix_forget_child(int pid);

extern int __libposix_send_control_msg(int pid, char *msg);

extern PosixError __libposix_notify_signal_to_process(int pid, PosixSignalInfo *siginfo);

extern PosixError __libposix_receive_signal(PosixSignalInfo *siginfo);

extern PosixError __libposix_dispatch_signal(int pid, PosixSignalInfo* siginfo);

extern int __libposix_restart_syscall(void);

extern int __libposix_signal_blocked(PosixSignalInfo *siginfo);

extern int __libposix_run_signal_handler(SignalConf *c, PosixSignalInfo *siginfo);

extern void __libposix_reset_pending_signals(void);

extern void __libposix_set_close_on_exec(int fd, int close);

extern int __libposix_should_close_on_exec(int fd);

extern void __libposix_close_on_exec(void);

extern void __libposix_init_signal_handlers(void);

#define SIGNAL_MASK(signal) (1ULL<<((signal)-1))
#define SIGNAL_RAW_ADD(bitmask, signal) (bitmask |= SIGNAL_MASK(signal))
#define SIGNAL_RAW_DEL(bitmask, signal) (bitmask &= ~SIGNAL_MASK(signal))

typedef enum {
	PHProcessExited,	/* WRITE, for nannies, the child might not be a libposix application */
	PHCallingExec,		/* WRITE */

	PHSignalProcess,	/* WRITE */
	PHSignalGroup,		/* WRITE */
	PHSignalForeground,	/* WRITE, for the controller process */

	PHIgnoreSignal,		/* WRITE */
	PHEnableSignal,		/* WRITE */
	PHBlockSignals,		/* WRITE */
	PHWaitSignals,		/* READ, may block */

	PHGetSessionId,		/* WRITE, ret contains the id */
	PHGetProcessGroupId,	/* WRITE, ret contains the id */
	PHGetPendingSignals,	/* WRITE, ret contains the mask */
	PHGetProcMask,		/* WRITE, ret contains the mask */

	PHSetForegroundGroup,	/* WRITE */
	PHGetForegroundGroup,	/* WRITE */
	PHDetachSession,	/* WRITE */
	PHSetProcessGroup,	/* WRITE */
} PosixHelperCommand;
typedef struct {
	int			target;
	PosixHelperCommand	command;
} PosixHelperRequest;

extern void __libposix_sighelper_open(void);
extern long __libposix_sighelper_cmd(PosixHelperCommand command, int target);
extern long __libposix_sighelper_set(PosixHelperCommand command, PosixSignalMask signal_set);
extern long __libposix_sighelper_signal(PosixHelperCommand command, int target, PosixSignalInfo *siginfo);
extern long __libposix_sighelper_wait(PosixSignalMask set, PosixSignalInfo *siginfo);
extern long __libposix_sighelper_set_pgid(int target, int group_id);
