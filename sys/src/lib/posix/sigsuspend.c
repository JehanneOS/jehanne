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
POSIX_sigsuspend(int *errnop, const PosixSignalMask *mask)
{
	PosixSignalMask old;

	if(mask == nil){
		*errnop = __libposix_get_errno(PosixEFAULT);
		return -1;
	}

	if(POSIX_sigprocmask(errnop, PosixSPMSetMask, mask, &old) != 0)
		return -1;

	do
		sys_rendezvous((void*)~0, (void*)1);
	while(__libposix_restart_syscall());

	if(POSIX_sigprocmask(errnop, PosixSPMSetMask, &old, nil) != 0)
		return -1;

	*errnop = __libposix_get_errno(PosixEINTR);
	return -1;
}
