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

#include <u.h>
#include <libc.h>

static struct {
	QLp	*p;
	QLp	x[1024];
} ql = {
	ql.x
};

/* The possible state transitions of a QLp are
 *
 * 	Free -> Queuing  -> Timedout|Done -> Free
 * 	Free -> QueuingR -> Timedout|Done -> Free
 * 	Free -> QueuingW -> Timedout|Done -> Free
 * 	Free -> Sleeping -> Timedout      -> Free
 *	Free -> Sleeping -> Queuing       -> Done -> Free
 *
 * QLp starts as Free and move to one of the custom states on getqlp.
 * Timedout is optional and alternative to Done.
 * Timedout can only occur if the QLp was allocated by a variant that
 * support timeouts (qlockt, rlockt, wlockt and rsleept).
 * Done is reached just before wakeup if and only if the current state
 * is the expected custom one (all transitions use CAS).
 *
 * All QLp need to reach the Free state to be released.
 *
 * All transitions are protected by a lock, except
 * - the ones that lead to Timedout, so that if a QLp is not in
 *   the expected state, it's Timedout.
 * - the ones from Done to Free since Done is
 *   a terminal state: Freeing is just book keeping.
 *
 * NOTE: The rsleep/rwakeup transitions mean that a you should never
 * use a qlockt a Qlock that is going to be used with a rsleept.
 */
enum
{
	Free,
	Queuing,
	QueuingR,
	QueuingW,
	Sleeping,
	Timedout,
	Done,
};

#if 0
typedef struct RendezvousLog
{
	void *tag;
	long pid;
	long start;
	long end;
	void *addr;
	void *caller;
	void *r;
} RendezvousLog;
RendezvousLog logs[1024];
int logidx;
static void*
debugrendezvous(void *tag, void *val)
{
	static void** pidp;
	int i;

	if(pidp == nil)
		pidp = jehanne_privalloc();
	if(*pidp == 0)
		*pidp = (void*)(long)jehanne_getpid();

	i = jehanne_ainc(&logidx) - 1;
	logs[i].tag = tag;
	logs[i].pid = (long)*pidp;
	logs[i].start = jehanne_nsec();
	logs[i].addr = __builtin_return_address(0);
	logs[i].caller = __builtin_return_address(1);
	logs[i].r = (void*)0xffabcdefffabcdef;
	logs[i].r = rendezvous(tag, (void*)logs[i].addr);
	logs[i].end = jehanne_nsec();
	return logs[i].r;
}
void
printdebugrendezvouslogs(void)
{
	int i;
	for(i = 0; i < logidx; ++i)
		jehanne_print("[%d] %#p @ %#p rendezvous(%#p, %#p) -> %#p @ %#p\n", logs[i].pid, logs[i].caller, logs[i].start, logs[i].tag, logs[i].addr, logs[i].r, logs[i].end);
}
static void*	(*_rendezvousp)(void*, void*) = debugrendezvous;
#else
static void*
__rendezvous(void* tag, void* val)
{
	return rendezvous(tag, val);
}
static void*	(*_rendezvousp)(void*, void*) = __rendezvous;
#endif


/* this gets called by the thread library ONLY to get us to use its rendezvous */
void
jehanne__qlockinit(void* (*r)(void*, void*))
{
	_rendezvousp = r;
}

/* find a free shared memory location to queue ourselves in */
static QLp*
getqlp(uint8_t use)
{
	QLp *p, *op;

	op = ql.p;
	for(p = op+1; ; p++){
		if(p == &ql.x[nelem(ql.x)])
			p = ql.x;
		if(p == op)
			abort();
		if(cas(&p->state, Free, use)){
			ql.p = p;
			p->next = nil;
			break;
		}
	}
	return p;
}

void
jehanne_qlock(QLock *q)
{
	QLp *p, *mp;

	jehanne_lock(&q->lock);
	if(!q->locked){
		q->locked = 1;
		jehanne_unlock(&q->lock);
		return;
	}


	/* chain into waiting list */
	mp = getqlp(Queuing);
	p = q->tail;
	if(p == nil)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	jehanne_unlock(&q->lock);

	/* wait */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;
	if(mp->state != Done){
		jehanne_print("mp %#p: state %d pc %#p\n", mp, mp->state, __builtin_return_address(0));
		abort();
	}
	mp->state = Free;
}

void
jehanne_qunlock(QLock *q)
{
	QLp *p;

	jehanne_lock(&q->lock);
	if (q->locked == 0)
		jehanne_fprint(2, "qunlock called with qlock not held, from %#p\n",
			jehanne_getcallerpc());
	p = q->head;
	while(p != nil && !cas(&p->state, Queuing, Done)){
		/* the lock in p timed out */
		if(p->state != Timedout){
			jehanne_print("qunlock mp %#p: state %d (should be Timedout) pc %#p\n", p, p->state, (uintptr_t)__builtin_return_address(0));
			abort();
		}
		p->state = Free;
		p = p->next;
	}
	if(p != nil){
		/* wakeup head waiting process */
		if(p->state != Done){
			jehanne_print("qunlock: p %#p p->state %d\n", p, p->state);
			abort();
		}
		q->head = p->next;
		if(q->head == nil)
			q->tail = nil;
		jehanne_unlock(&q->lock);
		while((*_rendezvousp)(p, (void*)0x12345) == (void*)~0)
			;
		return;
	} else {
		/* all pending locks timedout */
		q->head = nil;
		q->tail = nil;
	}
	q->locked = 0;
	jehanne_unlock(&q->lock);
}

int
jehanne_qlockt(QLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!jehanne_lockt(&q->lock, ms)){
		jehanne_forgivewkp(wkup);
		return 0;
	}

	if(!q->locked){
		jehanne_forgivewkp(wkup);
		q->locked = 1;
		jehanne_unlock(&q->lock);
		return 1;
	}

	/* chain into waiting list */
	mp = getqlp(Queuing);
	p = q->tail;
	if(p == nil)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;

	jehanne_unlock(&q->lock);

	/* wait */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (jehanne_awakened(wkup)){
			/* interrupted by awake */
			if(cas(&mp->state, Queuing, Timedout))
				/* if we can atomically mark the QLp
				 * the next qunlock will release it...
				 */
				return 0;
			/* ... otherwise we are going to take the lock
			 * on the next rendezvous from qunlock
			 */
		}

	jehanne_forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
jehanne_canqlock(QLock *q)
{
	if(!jehanne_canlock(&q->lock))
		return 0;
	if(!q->locked){
		q->locked = 1;
		jehanne_unlock(&q->lock);
		return 1;
	}
	jehanne_unlock(&q->lock);
	return 0;
}

void
jehanne_rlock(RWLock *q)
{
	QLp *p, *mp;

	jehanne_lock(&q->lock);
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->_readers++;
		jehanne_unlock(&q->lock);
		return;
	}

	mp = getqlp(QueuingR);
	p = q->tail;
	if(p == 0)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	mp->next = nil;
	jehanne_unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;
	if(mp->state != Done)
		abort();
	mp->state = Free;
}

int
jehanne_rlockt(RWLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!jehanne_lockt(&q->lock, ms)) {
		jehanne_forgivewkp(wkup);
		return 0;
	}

	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		jehanne_forgivewkp(wkup);
		q->_readers++;
		jehanne_unlock(&q->lock);
		return 1;
	}

	/* chain into waiting list */
	mp = getqlp(QueuingR);
	p = q->tail;
	if(p == 0)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	mp->next = nil;

	jehanne_unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (jehanne_awakened(wkup)){
			/* interrupted by awake */
			if(cas(&mp->state, QueuingR, Timedout))
				/* if we can atomically mark the QLp
				 * a future wunlock will release it...
				 */
				return 0;
			/* ... otherwise we are going to take the lock
			 * on the next rendezvous from wunlock
			 */
		}

	jehanne_forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
jehanne_canrlock(RWLock *q)
{
	jehanne_lock(&q->lock);
	if (q->writer == 0 && q->head == nil) {
		/* no writer; go for it */
		q->_readers++;
		jehanne_unlock(&q->lock);
		return 1;
	}
	jehanne_unlock(&q->lock);
	return 0;
}

void
jehanne_runlock(RWLock *q)
{
	QLp *p;

	jehanne_lock(&q->lock);
	if(q->_readers <= 0)
		abort();
	p = q->head;
	if(--(q->_readers) > 0 || p == nil){
runlockWithoutWriters:
		jehanne_unlock(&q->lock);
		return;
	}

	/* start waiting writer */
	while(p != nil && !cas(&p->state, QueuingW, Done)){
		/* the lock in p timed out
		 *
		 * Note that p cannot have reached Done or Free already
		 * since we hold q->lock, and the only transactions that
		 * do not require this lock are timeout ones.
		 */
		if(p->state != Timedout)
			abort();
		p->state = Free;
		p = p->next;
	}
	if(p == nil)
		goto runlockWithoutWriters;

	q->head = p->next;
	if(q->head == 0)
		q->tail = 0;
	q->writer = 1;
	jehanne_unlock(&q->lock);

	/* wakeup waiter */
	while((*_rendezvousp)(p, 0) == (void*)~0)
		;
}

void
jehanne_wlock(RWLock *q)
{
	QLp *p, *mp;

	jehanne_lock(&q->lock);
	if(q->_readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		q->writer = 1;
		jehanne_unlock(&q->lock);
		return;
	}

	/* chain into waiting list */
	p = q->tail;
	mp = getqlp(QueuingW);
	if(p == nil)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	mp->next = nil;
	jehanne_unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;

	if(mp->state != Done)
		abort();
	mp->state = Free;
}


int
jehanne_wlockt(RWLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!jehanne_lockt(&q->lock, ms)) {
		jehanne_forgivewkp(wkup);
		return 0;
	}

	if(q->_readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		jehanne_forgivewkp(wkup);
		q->writer = 1;
		jehanne_unlock(&q->lock);
		return 1;
	}

	/* chain into waiting list */
	p = q->tail;
	mp = getqlp(QueuingW);
	if(p == nil)
		q->head = mp;
	else
		p->next = mp;
	q->tail = mp;
	mp->next = nil;

	jehanne_unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (jehanne_awakened(wkup)){
			/* interrupted by awake */
			if(cas(&mp->state, QueuingW, Timedout))
				/* if we can atomically mark the QLp
				 * a future runlock/wunlock will release it...
				 */
				return 0;
			/* ... otherwise we are going to take the lock
			 * on the next rendezvous from runlock/wunlock
			 */
		}
	jehanne_forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
jehanne_canwlock(RWLock *q)
{
	jehanne_lock(&q->lock);
	if (q->_readers == 0 && q->writer == 0) {
		/* no one waiting; go for it */
		q->writer = 1;
		jehanne_unlock(&q->lock);
		return 1;
	}
	jehanne_unlock(&q->lock);
	return 0;
}

void
jehanne_wunlock(RWLock *q)
{
	QLp *p, *tmp;

	jehanne_lock(&q->lock);
	if(q->writer == 0)
		abort();
	p = q->head;
	if(p == nil){
		q->writer = 0;
		jehanne_unlock(&q->lock);
		return;
	}

	while(p != nil){
wakeupPendingWriter:	/* when we jump here, we know p is not nil */
		switch(casv(&p->state, QueuingW, Done)){
			case QueuingW:
				/* start waiting writer */
				q->head = p->next;
				if(q->head == nil)
					q->tail = nil;
				jehanne_unlock(&q->lock);
				while((*_rendezvousp)(p, 0) == (void*)~0)
					;
				return;
			case Timedout:
				/* R and W have the same fate, once Timedout */
				p->state = Free;
				p = p->next;
				break;
			case QueuingR:
				/* wakeup pending readers */
				goto wakeupPendingReaders;
			default:
				jehanne_print("wunlock: %#p has state %d instead of QueuingW\n", p, p->state);
				abort();
				break;
		}
	}

	/* wake waiting readers */
	while(p != nil){
wakeupPendingReaders:	/* when we jump here, we know p is not nil */
		switch(casv(&p->state, QueuingR, Done)){
			case QueuingR:
				q->_readers++;
				tmp = p->next;
				while((*_rendezvousp)(p, 0) == (void*)~0)
					;
				/* after the rendezvous, p->state will be set to Free
				 * from the reader we are going to wakeup, thus p could
				 * be reused before we are scheduled again: we need tmp
				 * to keep track of the next QLp to test
				 */
				p = tmp;
				break;
			case Timedout:
				p->state = Free;
				p = p->next;
				break;
			case QueuingW:
				if(q->_readers > 0){
					goto allPendingReadersStarted;
				} else {
					/* all readers timedout: wakeup the pending writer */
					goto wakeupPendingWriter;
				}
			default:
				jehanne_print("wunlock: %#p has state %d instead of QueuingR\n", p, p->state);
				abort();
				break;
		}
	}

allPendingReadersStarted:
	q->head = p;
	if(q->head == nil)
		q->tail = nil;
	q->writer = 0;
	jehanne_unlock(&q->lock);
}

void
jehanne_rsleep(Rendez *r)
{
	QLp *t, *me;

	if(!r->l)
		abort();
	jehanne_lock(&r->l->lock);
	/* we should hold the qlock */
	if(!r->l->locked)
		abort();

	/* add ourselves to the wait list */
	me = getqlp(Sleeping);
	if(r->head == nil)
		r->head = me;
	else
		r->tail->next = me;
	me->next = nil;
	r->tail = me;

	/* pass the qlock to the next guy */
	t = r->l->head;
	while(t != nil && !cas(&t->state, Queuing, Done)){
		/* the lock in t timed out */
		if(t->state != Timedout){
			jehanne_print("rsleep mp %#p: state %d (should be Timedout) pc %#p\n", t, t->state, __builtin_return_address(0));
			abort();
		}
		t->state = Free;
		t = t->next;
	}
	if(t != nil){
		r->l->head = t->next;
		if(r->l->head == nil)
			r->l->tail = nil;
		jehanne_unlock(&r->l->lock);
		while((*_rendezvousp)(t, (void*)0x12345) == (void*)~0)
			;
	}else{
		r->l->head = nil;
		r->l->tail = nil;
		r->l->locked = 0;
		jehanne_unlock(&r->l->lock);
	}

	/* wait for a wakeup */
	while((*_rendezvousp)(me, (void*)1) == (void*)~0)
		;
	if(me->state != Done)
		abort();
	me->state = Free;
}

int
jehanne_rsleept(Rendez *r, uint32_t ms)
{
	QLp *t, *me;
	int64_t wkup;

	if(!r->l)
		abort();

	if(!jehanne_lockt(&r->l->lock, ms))
		return 0;

	/* we should hold the qlock */
	if(!r->l->locked)
		abort();

	/* add ourselves to the wait list */
	me = getqlp(Sleeping);
	if(r->head == nil)
		r->head = me;
	else
		r->tail->next = me;
	me->next = nil;
	r->tail = me;

	/* pass the qlock to the next guy */
	t = r->l->head;
	while(t != nil && !cas(&t->state, Queuing, Done)){
		/* the lock in t timed out */
		if(t->state != Timedout){
			jehanne_print("rsleept mp %#p: state %d (should be Timedout) pc %#p\n", t, t->state, __builtin_return_address(0));
			abort();
		}
		t->state = Free;
		t = t->next;
	}
	if(t != nil){
		r->l->head = t->next;
		if(r->l->head == nil)
			r->l->tail = nil;
		jehanne_unlock(&r->l->lock);

		while((*_rendezvousp)(t, (void*)0x12345) == (void*)~0)
			;
	}else{
		r->l->head = nil;
		r->l->tail = nil;
		r->l->locked = 0;
		jehanne_unlock(&r->l->lock);
	}

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	/* wait for a rwakeup (or a timeout) */
	while((*_rendezvousp)(me, (void*)1) == (void*)~0)
		if (jehanne_awakened(wkup)){
			if(cas(&me->state, Sleeping, Timedout)){
				/* if we can atomically mark the QLp
				 * a future rwakeup will release it...
				 */
				jehanne_qlock(r->l);
				return 0;
			}
			/* ... otherwise we are going to take the lock
			 * on the next rendezvous from rwakeup
			 */
		}

	jehanne_forgivewkp(wkup);
	if(me->state != Done)
		abort();
	me->state = Free;

	return 1;
}

int
jehanne_rwakeup(Rendez *r)
{
	QLp *t;

	/*
	 * take off wait and put on front of queue
	 * put on front so guys that have been waiting will not get starved
	 */

	if(!r->l)
		abort();
	jehanne_lock(&r->l->lock);
	if(!r->l->locked)
		abort();

	if(r->head && r->head->state == Free)
		r->head = nil;

	t = r->head;

	while(t != nil && !cas(&t->state, Sleeping, Queuing)){
		if(t->state != Timedout){
			jehanne_print("rwakeup mp %#p: state %d (should be Timedout) pc %#p\n", t, t->state, __builtin_return_address(0));
			abort();
		}
		t->state = Free;
		t = t->next;
	}
	if(t == nil){
		r->head = nil;
		r->tail = nil;
		jehanne_unlock(&r->l->lock);
		return 0;
	}
	r->head = t->next;
	if(r->head == nil)
		r->tail = nil;

	t->next = r->l->head;
	r->l->head = t;
	if(r->l->tail == nil)
		r->l->tail = t;

	jehanne_unlock(&r->l->lock);
	return 1;
}

int
jehanne_rwakeupall(Rendez *r)
{
	int i;

	for(i=0; jehanne_rwakeup(r); i++)
		;
	return i;
}

