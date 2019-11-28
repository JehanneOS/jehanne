/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _APW_STDLIB_H
#define _APW_STDLIB_H
#ifndef HIDE_JEHANNE_APW
#include "libc.wrapper.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1	// POSIX

#include <posix.h>	// to get __POSIX_EXIT_PREFIX

typedef	struct {
	int	quot;
	int	rem;
} div_t;

typedef struct {
	long	quot;
	long	rem;
} ldiv_t;

extern void exit(int status);

extern int system(const char *command);

extern div_t div(int num, int denom);

extern ldiv_t ldiv(long num, long denom);

extern void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
		int (*compar)(const void*, const void*));

#undef abort
extern void abort(void);

#undef L_eprintf
extern void __eprintf(const char *format, const char *file,
		unsigned int line, const char *expression);

#endif
#endif
