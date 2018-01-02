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

#include "ureg.h"

struct Timers
{
	Lock	l;
	Timer	*head;
};

static Timers timers[MACHMAX];


static int64_t
tadd(Timers *tt, Timer *nt)
{
	int64_t when;
	Timer *t, **last;

	/* Called with tt locked */
	assert(nt->tt == nil);
	switch(nt->tmode){
	default:
		panic("timer");
		break;
	case Trelative:
		if(nt->tns <= 0)
			nt->tns = 1;
		nt->twhen = fastticks(nil) + ns2fastticks(nt->tns);
		break;
	case Tperiodic:
		/*
		 * Periodic timers must have a period of at least 100µs.
		 */
		assert(nt->tns >= 100000);
		if(nt->twhen == 0){
			/*
			 * Look for another timer at the
			 * same frequency for combining.
			 */
			for(t = tt->head; t; t = t->tnext){
				if(t->tmode == Tperiodic && t->tns == nt->tns)
					break;
			}
			if(t)
				nt->twhen = t->twhen;
			else
				nt->twhen = fastticks(nil);
		}

		/*
		 * The new time must be in the future.
		 * ns2fastticks() can return 0 if the tod clock
		 * has been adjusted by, e.g. timesync.
		 */
		when = ns2fastticks(nt->tns);
		if(when == 0)
			when = 1;
		nt->twhen += when;
		break;
	}

	for(last = &tt->head; t = *last; last = &t->tnext){
		if(t->twhen > nt->twhen)
			break;
	}
	nt->tnext = *last;
	*last = nt;
	nt->tt = tt;
	if(last == &tt->head)
		return nt->twhen;
	return 0;
}

static int64_t
tdel(Timer *dt)
{
	Timer *t, **last;
	Timers *tt;

	tt = dt->tt;
	if(tt == nil)
		return 0;
	for(last = &tt->head; t = *last; last = &t->tnext){
		if(t == dt){
			assert(dt->tt);
			dt->tt = nil;
			*last = t->tnext;
			break;
		}
	}
	if(last == &tt->head && tt->head)
		return tt->head->twhen;
	return 0;
}

/* add or modify a timer */
void
timeradd(Timer *nt)
{
	Timers *tt;
	int64_t when;

	/* Must lock Timer struct before Timers struct */
	ilock(&nt->l);
	if(tt = nt->tt){
		ilock(&tt->l);
		tdel(nt);
		iunlock(&tt->l);
	}
	tt = &timers[m->machno];
	ilock(&tt->l);
	when = tadd(tt, nt);
	if(when)
		timerset(when);
	iunlock(&tt->l);
	iunlock(&nt->l);
}


void
timerdel(Timer *dt)
{
	Timers *tt;
	int64_t when;

	ilock(&dt->l);
	if(tt = dt->tt){
		ilock(&tt->l);
		when = tdel(dt);
		if(when && tt == &timers[m->machno])
			timerset(tt->head->twhen);
		iunlock(&tt->l);
	}
	iunlock(&dt->l);
}

void
hzclock(Ureg *ur)
{
	m->ticks++;
	if(m->machno == 0)
		sys->ticks = m->ticks;

	if(m->proc)
		m->proc->pc = ur->ip;

	checkflushmmu();
	accounttime();
	kmapinval();

	if(kproftimer != nil)
		kproftimer(ur->ip);

	if(!m->online)
		return;

	if(active.exiting) {
		iprint("someone's exiting\n");
		exit(0);
	}

	if(m->machno == 0){
		checkalarms();
		awake_tick(m->ticks);
	}

	if(up && up->state == Running)
		hzsched();	/* in proc.c */
}

void
timerintr(Ureg *u, void* _1)
{
	Timer *t;
	Timers *tt;
	int64_t when, now;
	int callhzclock;

	callhzclock = 0;
	tt = &timers[m->machno];
	now = fastticks(nil);
	ilock(&tt->l);
	while(t = tt->head){
		/*
		 * No need to ilock t here: any manipulation of t
		 * requires tdel(t) and this must be done with a
		 * lock to tt held.  We have tt, so the tdel will
		 * wait until we're done
		 */
		when = t->twhen;
		if(when > now){
			timerset(when);
			iunlock(&tt->l);
			if(callhzclock)
				hzclock(u);
			return;
		}
		tt->head = t->tnext;
		assert(t->tt == tt);
		t->tt = nil;
		iunlock(&tt->l);
		if(t->tf)
			(*t->tf)(u, t);
		else
			callhzclock++;
		ilock(&tt->l);
		if(t->tmode == Tperiodic)
			tadd(tt, t);
	}
	iunlock(&tt->l);
}

void
timersinit(void)
{
	Timer *t;

	/*
	 * T->tf == nil means the HZ clock for this processor.
	 */
	todinit();
	t = jehanne_malloc(sizeof(*t));
	if(t == nil)
		panic("timersinit: no memory for Timer");
	t->tmode = Tperiodic;
	t->tt = nil;
	t->tns = 1000000000/HZ;
	t->tf = nil;
	timeradd(t);
}

Timer*
addclock0link(void (*f)(void), int ms)
{
	Timer *nt;
	int64_t when;

	/* Synchronize to hztimer if ms is 0 */
	nt = jehanne_malloc(sizeof(Timer));
	if(nt == nil)
		panic("addclock0link: no memory for Timer");
	if(ms == 0)
		ms = 1000/HZ;
	nt->tns = (int64_t)ms*1000000LL;
	nt->tmode = Tperiodic;
	nt->tt = nil;
	nt->tf = (void (*)(Ureg*, Timer*))f;

	ilock(&timers[0].l);
	when = tadd(&timers[0], nt);
	if(when)
		timerset(when);
	iunlock(&timers[0].l);
	return nt;
}
