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

static Thread	*runthread(Proc*);

static char *_psstate[] = {
	"Moribund",
	"Dead",
	"Exec",
	"Fork",
	"Running",
	"Ready",
	"Rendezvous",
};

static char*
psstate(int s)
{
	if(s < 0 || s >= nelem(_psstate))
		return "unknown";
	return _psstate[s];
}

void
_schedinit(void *arg)
{
	Proc *p;
	Thread *t, **l;

	p = arg;
	_threadsetproc(p);
	p->pid = jehanne_getpid();
	while(jehanne_setjmp(p->sched))
		;
	_threaddebug(DBGSCHED, "top of schedinit, _threadexitsallstatus=%p", _threadexitsallstatus);
	if(_threadexitsallstatus)
		jehanne_exits(_threadexitsallstatus);
	jehanne_lock(&p->lock);
	if((t=p->thread) != nil){
		p->thread = nil;
		if(t->moribund){
			t->state = Dead;
			for(l=&p->threads.head; *l; l=&(*l)->nextt)
				if(*l == t){
					*l = t->nextt;
					if(*l==nil)
						p->threads.tail = l;
					p->nthreads--;
					break;
				}
			jehanne_unlock(&p->lock);
			if(t->inrendez){
				_threadflagrendez(t);
				_threadbreakrendez();
			}
			jehanne_free(t->stk);
			jehanne_free(t->cmdname);
			jehanne_free(t);	/* XXX how do we know there are no references? */
			t = nil;
			_sched();
		}
		if(p->needexec){
			t->ret = _schedexec(&p->exec);
			p->needexec = 0;
		}
		if(p->newproc){
			t->ret = _schedfork(p->newproc);
			p->newproc = nil;
		}
		t->state = t->nextstate;
		if(t->state == Ready)
			_threadready(t);
	}
	jehanne_unlock(&p->lock);
	_sched();
}

void
needstack(int n)
{
	int x;
	Proc *p;
	Thread *t;
	
	p = _threadgetproc();
	t = p->thread;
	
	if((uint8_t*)&x - n < (uint8_t*)t->stk){
		jehanne_fprint(2, "%s %lud: &x=%p n=%d t->stk=%p\n",
			argv0, jehanne_getpid(), &x, n, t->stk);
		jehanne_fprint(2, "%s %lud: stack overflow\n", argv0, jehanne_getmainpid());
		abort();
	}
}

void
_sched(void)
{
	Proc *p;
	Thread *t;

Resched:
	p = _threadgetproc();
	if((t = p->thread) != nil){
		needstack(128);
		_threaddebug(DBGSCHED, "pausing, state=%s", psstate(t->state));
		if(jehanne_setjmp(t->sched)==0)
			jehanne_longjmp(p->sched, 1);
		return;
	}else{
		t = runthread(p);
		if(t == nil){
			_threaddebug(DBGSCHED, "all threads gone; exiting");
			_schedexit(p);
		}
		_threaddebug(DBGSCHED, "running %d.%d", t->proc->pid, t->id);
		p->thread = t;
		if(t->moribund){
			_threaddebug(DBGSCHED, "%d.%d marked to die");
			goto Resched;
		}
		t->state = Running;
		t->nextstate = Ready;
		jehanne_longjmp(t->sched, 1);
	}
}

static Thread*
runthread(Proc *p)
{
	Thread *t;
	Tqueue *q;

	if(p->nthreads==0)
		return nil;
	q = &p->ready;
	jehanne_lock(&p->readylock);
	if(q->head == nil){
		q->asleep = 1;
		_threaddebug(DBGSCHED, "sleeping for more work");
		jehanne_unlock(&p->readylock);
		while(sys_rendezvous(q, 0) == (void*)~0){
			if(_threadexitsallstatus)
				jehanne_exits(_threadexitsallstatus);
		}
		/* lock picked up from _threadready */
	}
	t = q->head;
	q->head = t->next;
	jehanne_unlock(&p->readylock);
	return t;
}

void
_threadready(Thread *t)
{
	Tqueue *q;

	assert(t->state == Ready);
	_threaddebug(DBGSCHED, "readying %d.%d", t->proc->pid, t->id);
	q = &t->proc->ready;
	jehanne_lock(&t->proc->readylock);
	t->next = nil;
	if(q->head==nil)
		q->head = t;
	else
		*q->tail = t;
	q->tail = &t->next;
	if(q->asleep){
		q->asleep = 0;
		/* lock passes to runthread */
		_threaddebug(DBGSCHED, "waking process %d", t->proc->pid);
		while(sys_rendezvous(q, 0) == (void*)~0){
			if(_threadexitsallstatus)
				jehanne_exits(_threadexitsallstatus);
		}
	}else
		jehanne_unlock(&t->proc->readylock);
}

void
yield(void)
{
	_sched();
}

