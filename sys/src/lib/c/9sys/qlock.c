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
static void*
debugrendezvous(void *tag, void *val)
{
	void* r = rendezvous(tag, __builtin_return_address(0));
	print("rendezvous(%#p, %#p) -> %#p\n", tag, __builtin_return_address(0), r);
	return r;
}
static void*	(*_rendezvousp)(void*, void*) = debugrendezvous;
#else
static void*	(*_rendezvousp)(void*, void*) = rendezvous;
#endif


/* this gets called by the thread library ONLY to get us to use its rendezvous */
void
_qlockinit(void* (*r)(void*, void*))
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
qlock(QLock *q)
{
	QLp *p, *mp;

	lock(&q->lock);
	if(!q->locked){
		q->locked = 1;
		unlock(&q->lock);
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
	unlock(&q->lock);

	/* wait */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;
	if(mp->state != Done){
		print("mp %#p: state %d pc %#p\n", mp, mp->state, __builtin_return_address(0));
		abort();
	}
	mp->state = Free;
}

void
qunlock(QLock *q)
{
	QLp *p;

	lock(&q->lock);
	if (q->locked == 0)
		fprint(2, "qunlock called with qlock not held, from %#p\n",
			getcallerpc());
	p = q->head;
	while(p != nil && !cas(&p->state, Queuing, Done)){
		/* the lock in p timed out */
		if(p->state != Timedout){
			print("qunlock mp %#p: state %d (should be Timedout) pc %#p\n", p, p->state, (uintptr_t)__builtin_return_address(0));
			abort();
		}
		p->state = Free;
		p = p->next;
	}
	if(p != nil){
		/* wakeup head waiting process */
		if(p->state != Done){
			print("qunlock: p %#p p->state %d\n", p, p->state);
			abort();
		}
		q->head = p->next;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->lock);
		while((*_rendezvousp)(p, (void*)0x12345) == (void*)~0)
			;
		return;
	} else {
		/* all pending locks timedout */
		q->head = nil;
		q->tail = nil;
	}
	q->locked = 0;
	unlock(&q->lock);
}

int
qlockt(QLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!lockt(&q->lock, ms)){
		forgivewkp(wkup);
		return 0;
	}

	if(!q->locked){
		forgivewkp(wkup);
		q->locked = 1;
		unlock(&q->lock);
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

	unlock(&q->lock);

	/* wait */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (awakened(wkup)){
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

	forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
canqlock(QLock *q)
{
	if(!canlock(&q->lock))
		return 0;
	if(!q->locked){
		q->locked = 1;
		unlock(&q->lock);
		return 1;
	}
	unlock(&q->lock);
	return 0;
}

void
rlock(RWLock *q)
{
	QLp *p, *mp;

	lock(&q->lock);
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->_readers++;
		unlock(&q->lock);
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
	unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;
	if(mp->state != Done)
		abort();
	mp->state = Free;
}

int
rlockt(RWLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!lockt(&q->lock, ms)) {
		forgivewkp(wkup);
		return 0;
	}

	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		forgivewkp(wkup);
		q->_readers++;
		unlock(&q->lock);
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

	unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (awakened(wkup)){
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

	forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
canrlock(RWLock *q)
{
	lock(&q->lock);
	if (q->writer == 0 && q->head == nil) {
		/* no writer; go for it */
		q->_readers++;
		unlock(&q->lock);
		return 1;
	}
	unlock(&q->lock);
	return 0;
}

void
runlock(RWLock *q)
{
	QLp *p;

	lock(&q->lock);
	if(q->_readers <= 0)
		abort();
	p = q->head;
	if(--(q->_readers) > 0 || p == nil){
runlockWithoutWriters:
		unlock(&q->lock);
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
	unlock(&q->lock);

	/* wakeup waiter */
	while((*_rendezvousp)(p, 0) == (void*)~0)
		;
}

void
wlock(RWLock *q)
{
	QLp *p, *mp;

	lock(&q->lock);
	if(q->_readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		q->writer = 1;
		unlock(&q->lock);
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
	unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		;

	if(mp->state != Done)
		abort();
	mp->state = Free;
}


int
wlockt(RWLock *q, uint32_t ms)
{
	QLp *p, *mp;
	int64_t wkup;

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!lockt(&q->lock, ms)) {
		forgivewkp(wkup);
		return 0;
	}

	if(q->_readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		forgivewkp(wkup);
		q->writer = 1;
		unlock(&q->lock);
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

	unlock(&q->lock);

	/* wait in kernel */
	while((*_rendezvousp)(mp, (void*)1) == (void*)~0)
		if (awakened(wkup)){
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
	forgivewkp(wkup);
	if(mp->state != Done)
		abort();
	mp->state = Free;

	return 1;
}

int
canwlock(RWLock *q)
{
	lock(&q->lock);
	if (q->_readers == 0 && q->writer == 0) {
		/* no one waiting; go for it */
		q->writer = 1;
		unlock(&q->lock);
		return 1;
	}
	unlock(&q->lock);
	return 0;
}

void
wunlock(RWLock *q)
{
	QLp *p, *tmp;

	lock(&q->lock);
	if(q->writer == 0)
		abort();
	p = q->head;
	if(p == nil){
		q->writer = 0;
		unlock(&q->lock);
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
				unlock(&q->lock);
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
				print("wunlock: %#p has state %d instead of QueuingW\n", p, p->state);
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
				print("wunlock: %#p has state %d instead of QueuingR\n", p, p->state);
				abort();
				break;
		}
	}

allPendingReadersStarted:
	q->head = p;
	if(q->head == nil)
		q->tail = nil;
	q->writer = 0;
	unlock(&q->lock);
}

void
rsleep(Rendez *r)
{
	QLp *t, *me;

	if(!r->l)
		abort();
	lock(&r->l->lock);
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
			print("rsleep mp %#p: state %d (should be Timedout) pc %#p\n", t, t->state, __builtin_return_address(0));
			abort();
		}
		t->state = Free;
		t = t->next;
	}
	if(t != nil){
		r->l->head = t->next;
		if(r->l->head == nil)
			r->l->tail = nil;
		unlock(&r->l->lock);
		while((*_rendezvousp)(t, (void*)0x12345) == (void*)~0)
			;
	}else{
		r->l->head = nil;
		r->l->tail = nil;
		r->l->locked = 0;
		unlock(&r->l->lock);
	}

	/* wait for a wakeup */
	while((*_rendezvousp)(me, (void*)1) == (void*)~0)
		;
	if(me->state != Done)
		abort();
	me->state = Free;
}

int
rsleept(Rendez *r, uint32_t ms)
{
	QLp *t, *me;
	int64_t wkup;

	if(!r->l)
		abort();

	/* set up awake to interrupt rendezvous */
	wkup = awake(ms);

	if(!lockt(&r->l->lock, ms)){
		forgivewkp(wkup);
		return 0;
	}

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
			print("rsleep mp %#p: state %d (should be Timedout) pc %#p\n", t, t->state, __builtin_return_address(0));
			abort();
		}
		t->state = Free;
		t = t->next;
	}
	if(t != nil){
		r->l->head = t->next;
		if(r->l->head == nil)
			r->l->tail = nil;
		unlock(&r->l->lock);

		while((*_rendezvousp)(t, (void*)0x12345) == (void*)~0)
			;
	}else{
		r->l->head = nil;
		r->l->tail = nil;
		r->l->locked = 0;
		unlock(&r->l->lock);
	}

	/* wait for a rwakeup (or a timeout) */
	do
	{
		if (awakened(wkup)){
			if(cas(&me->state, Sleeping, Timedout))
				/* if we can atomically mark the QLp
				 * a future rwakeup will release it...
				 */
				return 0;
			/* ... otherwise we are going to take the lock
			 * on the next rendezvous from rwakeup
			 */
		}
	}
	while((*_rendezvousp)(me, (void*)1) == (void*)~0);

	forgivewkp(wkup);
	if(me->state != Done)
		abort();
	me->state = Free;

	return 1;
}

int
rwakeup(Rendez *r)
{
	QLp *t;

	/*
	 * take off wait and put on front of queue
	 * put on front so guys that have been waiting will not get starved
	 */
	
	if(!r->l)
		abort();
	lock(&r->l->lock);
	if(!r->l->locked)
		abort();

	t = r->head;
	
	while(t != nil && !cas(&t->state, Sleeping, Queuing)){
		if(t->state != Timedout)
			abort();
		t->state = Free;
		t = t->next;
	}
	if(t == nil){
		unlock(&r->l->lock);
		return 0;
	}
	r->head = t->next;
	if(r->head == nil)
		r->tail = nil;

	t->next = r->l->head;
	r->l->head = t;
	if(r->l->tail == nil)
		r->l->tail = t;

	unlock(&r->l->lock);
	return 1;
}

int
rwakeupall(Rendez *r)
{
	int i;

	for(i=0; rwakeup(r); i++)
		;
	return i;
}

