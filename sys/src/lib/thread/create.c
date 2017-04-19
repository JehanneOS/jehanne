/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

Pqueue _threadpq;

static int
nextID(void)
{
	static Lock l;
	static int id;
	int i;

	jehanne_lock(&l);
	i = ++id;
	jehanne_unlock(&l);
	return i;
}
	
/*
 * Create and initialize a new Thread structure attached to a given proc.
 */
static int
newthread(Proc *p, void (*f)(void *arg), void *arg, uint32_t stacksize,
	  char *name, int grp)
{
	int id;
	Thread *t;

	if(stacksize < 32)
		jehanne_sysfatal("bad stacksize %d", stacksize);
	t = _threadmalloc(sizeof(Thread), 1);
	t->stksize = stacksize;
	t->stk = _threadmalloc(stacksize, 0);
	jehanne_memset(t->stk, 0xFE, stacksize);
	_threadinitstack(t, f, arg);
	t->grp = grp;
	if(name)
		t->cmdname = jehanne_strdup(name);
	t->id = nextID();
	id = t->id;
	t->next = (Thread*)~0;
	t->proc = p;
	_threaddebug(DBGSCHED, "create thread %d.%d name %s", p->pid, t->id, name);
	jehanne_lock(&p->lock);
	p->nthreads++;
	if(p->threads.head == nil)
		p->threads.head = t;
	else
		*p->threads.tail = t;
	p->threads.tail = &t->nextt;
	t->nextt = nil;
	t->state = Ready;
	_threadready(t);
	jehanne_unlock(&p->lock);
	return id;
}

/* 
 * Create a new thread and schedule it to run.
 * The thread grp is inherited from the currently running thread.
 */
int
threadcreate(void (*f)(void *arg), void *arg, uint32_t stacksize)
{
	return newthread(_threadgetproc(), f, arg, stacksize, nil, threadgetgrp());
}

/*
 * Create and initialize a new Proc structure with a single Thread
 * running inside it.  Add the Proc to the global process list.
 */
Proc*
_newproc(void (*f)(void *arg), void *arg, uint32_t stacksize, char *name,
	 int grp, int rforkflag)
{
	Proc *p;

	p = _threadmalloc(sizeof *p, 1);
	p->pid = -1;
	p->rforkflag = rforkflag;
	newthread(p, f, arg, stacksize, name, grp);

	jehanne_lock(&_threadpq.lock);
	if(_threadpq.head == nil)
		_threadpq.head = p;
	else
		*_threadpq.tail = p;
	_threadpq.tail = &p->next;
	jehanne_unlock(&_threadpq.lock);
	return p;
}

int
procrfork(void (*f)(void *), void *arg, uint32_t stacksize, int rforkflag)
{
	Proc *p;
	int id;

	p = _threadgetproc();
	assert(p->newproc == nil);
	p->newproc = _newproc(f, arg, stacksize, nil, p->thread->grp, rforkflag);
	id = p->newproc->threads.head->id;
	_sched();
	return id;
}

int
proccreate(void (*f)(void*), void *arg, uint32_t stacksize)
{
	return procrfork(f, arg, stacksize, 0);
}

void
_freeproc(Proc *p)
{
	Thread *t, *nextt;

	for(t = p->threads.head; t; t = nextt){
		if(t->cmdname)
			jehanne_free(t->cmdname);
		assert(t->stk != nil);
		jehanne_free(t->stk);
		nextt = t->nextt;
		jehanne_free(t);
	}
	jehanne_free(p);
}

void
_freethread(Thread *t)
{
	Proc *p;
	Thread **l;

	p = t->proc;
	jehanne_lock(&p->lock);
	for(l=&p->threads.head; *l; l=&(*l)->nextt){
		if(*l == t){
			*l = t->nextt;
			if(*l == nil)
				p->threads.tail = l;
			break;
		}
	}
	jehanne_unlock(&p->lock);
	if (t->cmdname)
		jehanne_free(t->cmdname);
	assert(t->stk != nil);
	jehanne_free(t->stk);
	jehanne_free(t);
}

