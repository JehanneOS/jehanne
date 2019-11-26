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

static PosixTimevalReader __libposix_timeval_reader;
static PosixTimezoneReader __libposix_timezone_reader;

unsigned int
POSIX_alarm(int *errnop, unsigned int seconds)
{
	long r = sys_alarm(seconds * 1000);
	return r/1000;
}

int
POSIX_gettimeofday(int *errnop, void *timeval, void *timezone)
{
	Tm *t;
	PosixError e = 0;

	if(timeval == nil && timezone == nil){
		e = PosixEFAULT;
		goto FailWithError;
	}

	t = localtime(time(nil));

	if(timeval != nil){
		if(__libposix_timeval_reader == nil)
			sysfatal("libposix: uninitialzed timeval reader");
		e = __libposix_timeval_reader(timeval, t);
		if(e != 0)
			goto FailWithError;
	}
	if(timezone != nil){
		if(__libposix_timezone_reader == nil)
			sysfatal("libposix: uninitialzed timezone reader");
		e = __libposix_timezone_reader(timezone, t);
		if(e != 0)
			goto FailWithError;
	}
	return 0;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
libposix_set_timeval_reader(PosixTimevalReader reader)
{
	if(__libposix_initialized())
		return 0;
	if(reader == nil)
		return 0;
	__libposix_timeval_reader = reader;
	return 1;
}

int
libposix_set_timezone_reader(PosixTimezoneReader reader)
{
	if(__libposix_initialized())
		return 0;
	if(reader == nil)
		return 0;
	__libposix_timezone_reader = reader;
	return 1;
}
