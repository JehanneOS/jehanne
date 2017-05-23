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
extern Child **__libposix_child_list;
static int __initialized;

static void
libposix_check_configuration(void)
{
	__libposix_errors_check_conf();
	__libposix_files_check_conf();
	__libposix_processes_check_conf();
	__libposix_signal_check_conf();
}

void
libposix_init(int argc, char *argv[], PosixInit init)
{
	extern int main(int, char**);
	extern unsigned char *__signals_to_code_map;
	extern unsigned char *__code_to_signal_map;
	extern int *__handling_external_signal;
	extern int *__libposix_sigchld_target_pid;

	WaitList *wait_list;
	Child *child_list;
	int status;
	int error_codes[ERRNO_LAST-ERRNO_FIRST];
	unsigned char signals_to_code[256];
	unsigned char code_to_signal[256];
	int handling_signal;
	int sigchld_target_pid;

	assert(__initialized == 0);

	/* initialize PosixErrors map */
	memset(error_codes, 0, sizeof(error_codes));
	__libposix_errors_codes=error_codes;

	/* initialize wait_list; see also POSIX_fork and POSIX_exit */
	wait_list = nil;
	__libposix_wait_list = &wait_list;

	/* initialize child_list; used when SIGCHLD is enabled */
	child_list = nil;
	__libposix_wait_list = &child_list;

	/* initialize signal handling */
	memset(signals_to_code, 0, sizeof(signals_to_code));
	memset(code_to_signal, 0, sizeof(code_to_signal));
	handling_signal = 0;
	sigchld_target_pid = 0;
	__signals_to_code_map = signals_to_code;
	__code_to_signal_map = code_to_signal;
	__handling_external_signal = &handling_signal;
	__libposix_sigchld_target_pid = &sigchld_target_pid;
	if(!atnotify(__libposix_note_handler, 1))
		sysfatal("libposix: atnotify");

	init();

	libposix_check_configuration();

	__initialized = 1;
	status = main(argc, argv);
	POSIX_exit(status);
}

int
__libposix_initialized(void)
{
	return __initialized;
}
