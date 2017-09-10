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


int
POSIX_kill(int *errnop, int pid, int signo)
{
	PosixError perror;
	PosixSignalInfo siginfo;
	int errno;

	if(signo < 1 || signo > PosixNumberOfSignals){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	memset(&siginfo, 0, sizeof(PosixSignalInfo));
	siginfo.si_signo = signo;
	siginfo.si_pid = *__libposix_pid;
	siginfo.si_code = PosixSIUser;
	siginfo.si_uid = POSIX_getuid(&errno);
	if(pid == siginfo.si_pid)
		perror = __libposix_receive_signal(&siginfo);
	else
		perror = __libposix_dispatch_signal(pid, &siginfo);
	if(perror != 0){
		*errnop = __libposix_get_errno(perror);
		return -1;
	}
	return 0;
}
