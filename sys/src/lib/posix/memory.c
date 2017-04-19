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

void *
POSIX_sbrk(int *errnop, ptrdiff_t incr)
{
	void *p;

	p = segbrk(incr);
	if(p != (void*)-1)
		return p;
	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_sbrk);
	return (void*)-1;
}

void *
POSIX_malloc(int *errnop, size_t size)
{
	void *p;

	p = malloc(size);
	if(p != nil)
		return p;
	*errnop = __libposix_get_errno(PosixENOMEM);
	return nil;
}

void *
POSIX_realloc(int *errnop, void *ptr, size_t size)
{
	void *p;

	p = realloc(ptr, size);
	if(p != nil)
		return p;
	if(size == 0){
		if(ptr != nil){
			// POSIX 1-2008 requires an "implementation defined" errno
			*errnop = __libposix_get_errno(PosixEINVAL);
		}
	} else {
		*errnop = __libposix_get_errno(PosixENOMEM);
	}
	return nil;
}

void *
POSIX_calloc(int *errnop, size_t nelem, size_t size)
{
	void *p;

	size = size * nelem;
	if(size <= 0){
		// POSIX 1-2008 requires an "implementation defined" errno
		*errnop = __libposix_get_errno(PosixEINVAL);
		return nil;
	}
	p = malloc(size);
	if(p != nil){
		memset(p, 0, size);
		return p;
	}
	*errnop = __libposix_get_errno(PosixENOMEM);
	return nil;
}

void
POSIX_free(void *ptr)
{
	jehanne_free(ptr);
}
