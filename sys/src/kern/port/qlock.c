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

#include <ptrace.h>

struct {
	uint32_t rlock;
	uint32_t rlockq;
	uint32_t wlock;
	uint32_t wlockq;
	uint32_t qlock;
	uint32_t qlockq;
} rwstats;

void
qlock(QLock *q)
{
	Proc *p;
	void (*pt)(Proc*, int, int64_t, int64_t);

	if(m->ilockdepth != 0)
		print("qlock: %#p: ilockdepth %d", getcallerpc(), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("qlock: %#p: nlocks %d", getcallerpc(), up->nlocks);

	lock(&q->use);
	rwstats.qlock++;
	if(!q->locked) {
		q->locked = 1;
		q->qpc = getcallerpc();
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("qlock");
	rwstats.qlockq++;
	p = q->tail;
	if(p == 0)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = 0;
	up->state = Queueing;
	up->qpc = getcallerpc();
	if(up->trace && (pt = proctrace) != nil)
		pt(up, SSleep, 0, Queueing | (up->qpc<<8));
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
	q->qpc = getcallerpc();
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
	if(p){
		q->head = p->qnext;
		if(q->head == 0)
			q->tail = 0;
		unlock(&q->use);
		ready(p);
		return;
	}
	q->locked = 0;
	q->qpc = 0;
	unlock(&q->use);
}

void
priqlock(QLock *q)
{
	Proc *p;
	void (*pt)(Proc*, int, int64_t, int64_t);

	if(m->ilockdepth != 0)
		print("priqlock: %#p: ilockdepth %d\n", getcallerpc(), m->ilockdepth);
	if(up != nil && up->nlocks)
		print("priqlock: %#p: nlocks %d\n", getcallerpc(), up->nlocks);

	lock(&q->use);
	if(!q->locked) {
		//q->p = up;
		q->locked = 1;
		q->qpc = getcallerpc();
		unlock(&q->use);
		return;
	}
	if(up == nil)
		panic("priqlock");
//	if(q->p == up)
//		panic("qlock deadlock. pid=%ld cpc=%lux qpc=%lux\n", up->pid, getcallerpc(), up->qpc);
	p = up->qnext = q->head;
	if(p == nil)
		q->tail = up;
	q->head = up;
	up->state = Queueing;
	up->qpc = getcallerpc();
	if(up->trace && (pt = proctrace) != nil)
		pt(up, SSleep, 0, Queueing | (up->qpc<<8));
//	if(kproflock)
//		kproflock(up->qpc);
	unlock(&q->use);
	sched();
}

void
rlock(RWlock *q)
{
	Proc *p;
	void (*pt)(Proc*, int, int64_t, int64_t);
	uintptr_t pc;

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
	if(p == 0)
		q->head = up;
	else
		p->qnext = up;
	q->tail = up;
	up->qnext = 0;
	up->state = QueueingR;
	if(up->trace && (pt = proctrace) != nil){
		pc = getcallerpc();
		pt(up, SSleep, 0, QueueingR | (pc<<8));
	}
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
	if(q->head == 0)
		q->tail = 0;
	q->writer = 1;
	unlock(&q->use);
	ready(p);
}

void
wlock(RWlock *q)
{
	Proc *p;
	uintptr_t pc;
	void (*pt)(Proc*, int, int64_t, int64_t);

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
	up->qnext = 0;
	up->state = QueueingW;
	if(up->trace && (pt = proctrace) != nil){
		pc = getcallerpc();
		pt(up, SSleep, 0, QueueingW|(pc<<8));
	}
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
