/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
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
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * Print functions for system call tracing.
 */
void
fmtrwdata(Fmt* f, char* a, int n)
{
	int i;
	char *t;

	if(a == nil){
		jehanne_fmtprint(f, " 0x0%s");
		return;
	}
	a = validaddr(a, n, 0);
	t = smalloc(n+1);
	for(i = 0; i < n; i++){
		if(a[i] > 0x20 && a[i] < 0x7f)
			t[i] = a[i];
		else
			t[i] = '.';
	}
	t[n] = 0;

	jehanne_fmtprint(f, " %#p/\"%s\"", a, t);
	jehanne_free(t);
}

void
fmtuserstring(Fmt* f, const char* a)
{
	int n;
	char *t;

	if(a == nil){
		jehanne_fmtprint(f, " 0/\"\"");
		return;
	}
	a = validaddr((void*)a, 1, 0);
	n = ((char*)vmemchr((char*)a, 0, 0x7fffffff) - a) + 1;
	t = smalloc(n+1);
	jehanne_memmove(t, (char*)a, n);
	t[n] = 0;
	jehanne_fmtprint(f, " %#p/\"%s\"", a, t);
	jehanne_free(t);
}

void
fmtuserstringlist(Fmt* fmt, const char** argv)
{
	char* a;
	evenaddr(PTR2UINT(argv));
	for(;;){
		a = *(char**)validaddr((char**)argv, sizeof(char**), 0);
		if(a == nil)
			break;
		jehanne_fmtprint(fmt, " ");
		fmtuserstring(fmt, a);
		argv++;
	}
}

