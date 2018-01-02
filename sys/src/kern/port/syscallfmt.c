/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
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
		jehanne_fmtprint(f, " 0x0/\"\"");
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
