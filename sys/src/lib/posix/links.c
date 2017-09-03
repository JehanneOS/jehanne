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
#include <9P2000.h>
#include <posix.h>
#include "internal.h"

int
POSIX_lchown(int *errnop, const char *path, int owner, int group)
{
	/* TODO: implement when actually needed */
	return 0;
}

int
POSIX_link(int *errnop, const char *old, const char *new)
{
	int err;
	/* let choose the most appropriate error */
	if(old == nil || new == nil || old[0] == 0 || new[0] == 0)
		err = __libposix_get_errno(PosixENOENT);
	else if(access(new, AEXIST) == 0)
		err = __libposix_get_errno(PosixEEXIST);
	else if(access(old, AEXIST) == 0)
		err = __libposix_get_errno(PosixENOENT);
	else {
		/* Jehanne does not support links.
		 * A custom overlay filesystem might support them in
		 * the future but so far it does not exists yet.
		 *
		 * We return EXDEV so that a posix compliant caller
		 * can fallback to a simple copy.
		 */
		err = __libposix_get_errno(PosixEXDEV);
	}
	*errnop = err;
	return -1;
}

int
POSIX_lstat(int *errnop, const char *file, void *pstat)
{
	return POSIX_stat(errnop, file, pstat);
}

int
POSIX_readlink(int *errnop, const char *path, char *buf, int bufsize)
{
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}

int
POSIX_readlinkat(int *errnop, int fd, const char *path, char *buf, int bufsize)
{
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;
}
