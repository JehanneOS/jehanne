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
POSIX_getuid(int *errnop)
{
	return 0;
}

int
POSIX_geteuid(int *errnop)
{
	return 0;
}

int
POSIX_setuid(int *errnop, int uid)
{
	return 0;
}

int
POSIX_seteuid(int *errnop, int euid)
{
	return 0;
}

int
POSIX_setreuid(int *errnop, int ruid, int euid)
{
	return 0;
}

int
POSIX_getgid(int *errnop)
{
	return 0;
}

int
POSIX_getegid(int *errnop)
{
	return 0;
}

int
POSIX_setgid(int *errnop, int gid)
{
	return 0;
}

int
POSIX_setegid(int *errnop, int egid)
{
	return 0;
}

int
POSIX_setregid(int *errnop, int rgid, int egid)
{
	return 0;
}

int
POSIX_setpgid(int *errnop, int pid, int pgid)
{
	return 0;
}

int
POSIX_getsid(int *errnop, int pid)
{
	return 0;
}

int
POSIX_setsid(int *errnop)
{
	*errnop = __libposix_get_errno(PosixEPERM);
	return 0;
}

