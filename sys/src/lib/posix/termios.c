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
#include <envvars.h>
#include <posix.h>
#include "internal.h"

int
POSIX_tcgetpgrp(int *errnop, int fd)
{
	long pgrp;
	if(!POSIX_isatty(errnop, fd))
		return -1;

	pgrp = __libposix_sighelper_cmd(PHGetForegroundGroup, 0);
	if(pgrp <= 0)
		goto FailWithENOTTY;

	return pgrp;

FailWithENOTTY:
	*errnop = __libposix_get_errno(PosixENOTTY);
	return -1;
}

int
POSIX_tcsetpgrp(int *errnop, int fd, int pgrp)
{
	PosixError e;

	if(!POSIX_isatty(errnop, fd))
		return -1;
	if(pgrp < 0){
		e = PosixEINVAL;
		goto FailWithError;
	}
	if(__libposix_sighelper_cmd(PHSetForegroundGroup, pgrp) < 0){
		e = PosixEPERM;
		goto FailWithError;
	}
	return 0;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}
