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

#define	D(c)

static void
mcslock(Lock *lk, LockEntry *ql)
{
	LockEntry *pred;

	D('!');
	ql->next = nil;
	ql->locked = 1;
	pred = xchgm(&lk->head, ql);
	if(pred != nil){
		pred->next = ql;
		if(SUPPORT_MWAIT){
			while(mwait32(&ql->locked, 1) == 1)
				{}
		}else{
			while(ql->locked)
				pause();	/* spin, could monwait */
		}
	} else {
		/* nobody else wait for ql->locked to become zero,
		 * but keep things clean
		 */
		ql->locked = 0;
	}
}

static int
mcscanlock(Lock *lk, LockEntry *ql)
{
	D('?');
	ql->next = nil;
	ql->locked = 0;
	return CASV(&lk->head, nil, ql);
}

static LockEntry*
mcsunlock(Lock *lk, LockEntry *ql)
{
	D('#');
	if(ql->next != nil || !CASV(&lk->head, ql, nil)){
		/* successor, wait for list to catch up */
		while(ql->next == nil)
			{}
		xchg32(&ql->next->locked, 0);
	}
	return ql;
}

static LockEntry*
allocle(Lock *l, uintptr_t pc)
{
	LockEntry *a, *e;

	a = &m->locks[0];
	e = a + nelem(m->locks);
	while(a < e && !CASV(&a->used, nil, l))
		++a;
	if(a == e)
		panic("allocle: need more m->locks");
	a->pc = pc;
	a->p = up;
	a->m = m;
	a->isilock = 0;
	return a;
}

static LockEntry*
findle(Lock *l)
{
	LockEntry *a;

	a = l->e;
	if(a->used != l)
		panic("findle");
	return a;
}

int
lock(Lock *l)
{
	LockEntry *ql;

	if(up != nil)
		up->nlocks++;
	ql = allocle(l, getcallerpc());
	mcslock(l, ql);
	l->e = ql;
	return 0;
}

int
canlock(Lock *l)
{
	LockEntry *ql;

	if(up != nil)
		up->nlocks++;
	ql = allocle(l, getcallerpc());
	if(mcscanlock(l, ql)){
		l->e = ql;
		return 1;
	}
	ql->used = nil;
	if(up != nil)
		up->nlocks--;
	return 0;
}

void
unlock(Lock *l)
{
	LockEntry *ql;

	if(l->head == nil){
		jehanne_print("unlock: not locked: pc %#p\n", getcallerpc());
		return;
	}
	ql = findle(l);
	if(ql->isilock)
		panic("unlock of ilock: pc %#p", getcallerpc());
	if(ql->p != up)
		panic("unlock: up changed: pc %#p, acquired at pc %#p, lock p %#p, unlock up %#p",
			getcallerpc(), ql->pc, ql->p, up);
	mcsunlock(l, ql);
	ql->used = nil;
	if(up != nil && --up->nlocks == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

void
ilock(Lock *l)
{
	uintptr_t pc;
	Mreg s;
	LockEntry *ql;

	pc = getcallerpc();
	s = splhi();
	ql = allocle(l, pc);
	ql->isilock = 1;
	ql->sr = s;
	/* the old taslock code would splx(s) to allow interrupts while waiting (if not nested) */
	mcslock(l, ql);
	l->e = ql;
	m->ilockdepth++;
	m->ilockpc = pc;
	if(up != nil)
		up->lastilock = l;
}

void
iunlock(Lock *l)
{
	Mreg s;
	LockEntry *ql;

	if(islo())
		panic("iunlock while lo: pc %#p\n", getcallerpc());
	ql = findle(l);
	if(!ql->isilock)
		panic("iunlock of lock: pc %#p\n", getcallerpc());
	if(ql->m != m){
		panic("iunlock by cpu%d, locked by cpu%d: pc %#p\n",
			m->machno, ql->m->machno, getcallerpc());
	}
	mcsunlock(l, ql);
	s = ql->sr;
	ql->used = nil;
	m->ilockdepth--;
	if(up != nil)
		up->lastilock = nil;
	splx(s);
}

int
ownlock(Lock *l)
{
	int i;

	for(i = 0; i < nelem(m->locks); i++)
		if(m->locks[i].used == l)
			return 1;
	return 0;
}

uintptr_t
lockgetpc(Lock *l)
{
	LockEntry *ql;

	ql = l->e;
	if(ql != nil && ql->used == l)
		return ql->pc;
	return 0;
}

void
locksetpc(Lock *l, uintptr_t pc)
{
	LockEntry *ql;

	ql = l->e;
	if(ql != nil && ql->used == l && ql->m == m)
		ql->pc = pc;
}
