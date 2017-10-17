/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "rc.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

struct{
	void	(*f)(void);
	char	*name;
} fname[] = {
	Xappend, "Xappend",
	Xasync, "Xasync",
	Xbang, "Xbang",
	Xclose, "Xclose",
	Xdup, "Xdup",
	Xeflag, "Xeflag",
	Xexit, "Xexit",
	Xfalse, "Xfalse",
	Xifnot, "Xifnot",
	Xjump, "Xjump",
	Xmark, "Xmark",
	Xpopm, "Xpopm",
	Xrdwr, "Xrdwr",
	Xread, "Xread",
	Xreturn, "Xreturn",
	Xtrue, "Xtrue",
	Xif, "Xif",
	Xwastrue, "Xwastrue",
	Xword, "Xword",
	Xwrite, "Xwrite",
	Xmatch, "Xmatch",
	Xcase, "Xcase",
	Xconc, "Xconc",
	Xassign, "Xassign",
	Xdol, "Xdol",
	Xcount, "Xcount",
	Xlocal, "Xlocal",
	Xunlocal, "Xunlocal",
	Xfn, "Xfn",
	Xdelfn, "Xdelfn",
	Xpipe, "Xpipe",
	Xpipewait, "Xpipewait",
	Xpopredir, "Xpopredir",
	Xrdcmds, "Xrdcmds",
	(void (*)(void))Xerror, "Xerror",
	Xbackq, "Xbackq",
	Xpipefd, "Xpipefd",
	Xsubshell, "Xsubshell",
	Xdelhere, "Xdelhere",
	Xfor, "Xfor",
	Xglob, "Xglob",
	Xglobs, "Xglobs",
	Xrdfn, "Xrdfn",
	Xsimple, "Xsimple",
	Xqdol, "Xqdol",
0};

void
pfnc(io *fd, thread *t)
{
	int i;
	void (*fn)(void) = t->code[t->pc].f;
	list *a;

	pfmt(fd, "pid %d cycle %p %d ", getpid(), t->code, t->pc);
	for(i = 0; fname[i].f; i++) 
		if(fname[i].f == fn){
			pstr(fd, fname[i].name);
			break;
		}
	if(!fname[i].f)
		pfmt(fd, "%p", fn);
	for(a = t->argv; a; a = a->next) 
		pfmt(fd, " (%v)", a->words);
	pchr(fd, '\n');
	flush(fd);
}
