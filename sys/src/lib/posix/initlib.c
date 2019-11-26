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

extern int *__libposix_errors_codes;
extern WaitList **__libposix_wait_list;
extern ChildList **__libposix_child_list;
static int __initialized;
static PosixProcessDisposer __libposix_process_dispose;

int *__libposix_sigchld_father_pid;
int *__libposix_sigchld_child_pid;
int *__libposix_devsignal;
int *__libposix_pid;

static void
libposix_check_configuration(void)
{
	__libposix_errors_check_conf();
	__libposix_files_check_conf();
	__libposix_processes_check_conf();
}

void
__libposix_sighelper_open(void)
{
	int mypid;
	if(*__libposix_devsignal >= 0)
		sys_close(*__libposix_devsignal);
	mypid = *__libposix_pid;
	*__libposix_devsignal = sys_create("/dev/posix/signals", ORDWR|OCEXEC, mypid);
	if(*__libposix_devsignal < 0)
		sysfatal("cannot create /dev/posix/signals: %r");
}

void
libposix_init(int argc, char *argv[], PosixInit init)
{
	extern int main(int, char**);
	extern int *__handling_external_signal;
	extern int *__restart_syscall;
	extern PosixSignalMask *__libposix_signal_mask;

	WaitList *wait_list;
	ChildList *child_list;
	PosixSignalMask signal_mask;
	int mypid;
	int status;
	int error_codes[ERRNO_LAST-ERRNO_FIRST];
	int handling_signal;
	int sigchld_father_pid;
	int sigchld_child_pid;
	int restart_syscall;
	int devsignal;

	assert(__initialized == 0);

	mypid = getpid();
	__libposix_pid = &mypid;

	/* initialize PosixErrors map */
	memset(error_codes, 0, sizeof(error_codes));
	__libposix_errors_codes = error_codes;

	/* initialize wait_list; see also POSIX_fork and POSIX_exit */
	wait_list = nil;
	__libposix_wait_list = &wait_list;

	/* initialize child_list; used when SIGCHLD is enabled */
	child_list = nil;
	__libposix_child_list = &child_list;

	/* initialize signal handling */
	handling_signal = 0;
	sigchld_father_pid = 0;
	sigchld_child_pid = 0;
	signal_mask = 0;
	devsignal = -1;
	__libposix_devsignal = &devsignal;
	__restart_syscall = &restart_syscall;
	__handling_external_signal = &handling_signal;
	__libposix_sigchld_father_pid = &sigchld_father_pid;
	__libposix_sigchld_child_pid = &sigchld_child_pid;
	__libposix_signal_mask = &signal_mask;
	__libposix_reset_pending_signals();
	if(!atnotify(__libposix_note_handler, 1))
		sysfatal("libposix: atnotify");

	__libposix_sighelper_open();
	signal_mask = (PosixSignalMask)__libposix_sighelper_cmd(PHGetProcMask, 0);
	__libposix_init_signal_handlers();

	init(argc, argv);

	libposix_check_configuration();

	__initialized = 1;
	status = main(argc, argv);

	if(__libposix_process_dispose != nil)
		__libposix_process_dispose(status);

	POSIX_exit(status);
}

int
libposix_on_process_disposition(PosixProcessDisposer dispose)
{
	if(__libposix_process_dispose != nil)
		return 0;
	__libposix_process_dispose = dispose;
	return 1;
}

int
__libposix_initialized(void)
{
	return __initialized;
}

long
__libposix_sighelper_cmd(PosixHelperCommand command, int posix_process_pid)
{
	union {
		PosixHelperRequest	request;
		long			raw;
	} offset;

	offset.request.command = command;
	offset.request.target = posix_process_pid;

	return sys_pwrite(*__libposix_devsignal, "", 0, offset.raw);
}
