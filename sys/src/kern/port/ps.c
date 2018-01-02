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

void
pshash(Proc *p)
{
	int h;

	h = p->pid % nelem(procalloc.ht);
	lock(&procalloc.l);
	p->pidhash = procalloc.ht[h];
	procalloc.ht[h] = p;
	unlock(&procalloc.l);
}

void
psunhash(Proc *p)
{
	int h;
	Proc **l;

	h = p->pid % nelem(procalloc.ht);
	lock(&procalloc.l);
	for(l = &procalloc.ht[h]; *l != nil; l = &(*l)->pidhash)
		if(*l == p){
			*l = p->pidhash;
			break;
		}
	unlock(&procalloc.l);
}

int
psindex(int pid)
{
	Proc *p;
	int h;
	int s;

	s = -1;
	h = pid % nelem(procalloc.ht);
	lock(&procalloc.l);
	for(p = procalloc.ht[h]; p != nil; p = p->pidhash)
		if(p->pid == pid){
			s = p->index;
			break;
		}
	unlock(&procalloc.l);
	return s;
}

Proc*
debug_get_proc_by_pid(int pid)
{
	int i = psindex(pid);
	if(i < 0)
		return nil;
	return procalloc.arena + i;
}

Proc*
psincref(int i)
{
	/*
	 * Placeholder.
	 */
	if(i >= procalloc.nproc)
		return nil;
	return &procalloc.arena[i];
}

void
psdecref(Proc *p)
{
	/*
	 * Placeholder.
	 */
	USED(p);
}

void
psrelease(Proc* p)
{
	p->qnext = procalloc.free;
	procalloc.free = p;
}

Proc*
psalloc(void)
{
	Proc *p;
	char msg[64];

	lock(&procalloc.l);
	for(;;) {
		if(p = procalloc.free)
			break;

		unlock(&procalloc.l);
		jehanne_snprint(msg, sizeof msg, "no procs; %s forking",
			up? up->text: "kernel");
		resrcwait(msg, "Noprocs");
		lock(&procalloc.l);
	}
	procalloc.free = p->qnext;
	unlock(&procalloc.l);

	while(p->mach != nil || p->nlocks != 0)
		{}

	return p;
}

void
psinit(unsigned int nproc)
{
	Proc *p;
	int i;

	procalloc.nproc = nproc;
	procalloc.free = xalloc(nproc*sizeof(Proc));
	if(procalloc.free == nil)
		panic("cannot allocate %ud procs (%udMB)\n", nproc, nproc*sizeof(Proc)/(1024*1024));
	procalloc.arena = procalloc.free;

	p = procalloc.free;
	for(i=0; i<nproc-1; i++,p++){
		p->qnext = p+1;
		p->index = i;
	}
	p->qnext = 0;
	p->index = i;
}
