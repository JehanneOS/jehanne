/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include	"../port/error.h"

struct {
	uint32_t rlock;
	uint32_t rlockq;
	uint32_t wlock;
	uint32_t wlockq;
	uint32_t qlock;
	uint32_t qlockq;
} rwstats;

void
eqlock(QLock *q)
{
	Proc *p;

	if(m->ilockdepth != 0)
		print("eqlock: %#p: ilockdepth %d\n", getcallerpc(), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("eqlock: %#p: nlocks %d\n", getcallerpc(), up->nlocks);
	if(up != nil && up->eql != nil)
		print("eqlock: %#p: eql %p\n", getcallerpc(), up->eql);
	if(q->use.key == 0x55555555)
		panic("eqlock: q %#p, key 5*", q);

	lock(&q->use);
	rwstats.qlock++;
	if(!q->locked) {
		q->locked = 1;
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("eqlock");
	if(up->notepending){
		up->notepending = 0;
		unlock(&q->use);
		interrupted();
	}
	rwstats.qlockq++;
	p = q->tail;
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->eql = q;
	up->qnext = nil;
	up->qpc = getcallerpc();
	up->state = Queueing;
	unlock(&q->use);
	sched();
	if(up->eql == nil){
		up->notepending = 0;
		interrupted();
	}
	up->eql = nil;
}

void
qlock(QLock *q)
{
	Proc *p;

	if(m->ilockdepth != 0)
		print("qlock: %#p: ilockdepth %d\n", getcallerpc(), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("qlock: %#p: nlocks %d\n", getcallerpc(), up->nlocks);
	if(up != nil && up->eql != nil)
		print("qlock: %#p: eql %p\n", getcallerpc(), up->eql);
	if(q->use.key == 0x55555555)
		panic("qlock: q %#p, key 5*", q);
	lock(&q->use);
	rwstats.qlock++;
	if(!q->locked) {
		q->locked = 1;
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("qlock");
	rwstats.qlockq++;
	p = q->tail;
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->eql = nil;
	up->qnext = nil;
	up->state = Queueing;
	up->qpc = getcallerpc();
	unlock(&q->use);
	sched();
}

int
canqlock(QLock *q)
{
	if(!canlock(&q->use))
		return 0;
	if(q->locked){
		unlock(&q->use);
		return 0;
	}
	q->locked = 1;
	unlock(&q->use);
	return 1;
}

void
qunlock(QLock *q)
{
	Proc *p;

	lock(&q->use);
	if (q->locked == 0)
		print("qunlock called with qlock not held, from %#p\n",
			getcallerpc());
	p = q->head;
	if(p != nil){
		q->head = p->qnext;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->use);
		ready(p);
		return;
	}
	q->locked = 0;
	unlock(&q->use);
}

void
rlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	rwstats.rlock++;
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->readers++;
		unlock(&q->use);
		return;
	}

	rwstats.rlockq++;
	p = q->tail;
	if(up == nil)
		panic("rlock");
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = nil;
	up->state = QueueingR;
	unlock(&q->use);
	sched();
}

void
runlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	p = q->head;
	if(--(q->readers) > 0 || p == nil){
		unlock(&q->use);
		return;
	}

	/* start waiting writer */
	if(p->state != QueueingW)
		panic("runlock");
	q->head = p->qnext;
	if(q->head == nil)
		q->tail = nil;
	q->writer = 1;
	unlock(&q->use);
	ready(p);
}

void
wlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	rwstats.wlock++;
	if(q->readers == 0 && q->writer == 0){
		/* noone waiting, go for it */
		q->wpc = getcallerpc();
		q->wproc = up;
		q->writer = 1;
		unlock(&q->use);
		return;
	}

	/* wait */
	rwstats.wlockq++;
	p = q->tail;
	if(up == nil)
		panic("wlock");
	if(p == nil)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = nil;
	up->state = QueueingW;
	unlock(&q->use);
	sched();
}

void
wunlock(RWlock *q)
{
	Proc *p;

	lock(&q->use);
	p = q->head;
	if(p == nil){
		q->writer = 0;
		unlock(&q->use);
		return;
	}
	if(p->state == QueueingW){
		/* start waiting writer */
		q->head = p->qnext;
		if(q->head == nil)
			q->tail = nil;
		unlock(&q->use);
		ready(p);
		return;
	}

	if(p->state != QueueingR)
		panic("wunlock");

	/* waken waiting readers */
	while(q->head != nil && q->head->state == QueueingR){
		p = q->head;
		q->head = p->qnext;
		q->readers++;
		ready(p);
	}
	if(q->head == nil)
		q->tail = nil;
	q->writer = 0;
	unlock(&q->use);
}

/* same as rlock but punts if there are any writers waiting */
int
canrlock(RWlock *q)
{
	lock(&q->use);
	rwstats.rlock++;
	if(q->writer == 0 && q->head == nil){
		/* no writer, go for it */
		q->readers++;
		unlock(&q->use);
		return 1;
	}
	unlock(&q->use);
	return 0;
}
