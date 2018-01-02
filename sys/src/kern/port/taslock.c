/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
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
#include "../port/error.h"

long maxlockcycles;
long maxilockcycles;
long cumlockcycles;
long cumilockcycles;
uintptr_t maxlockpc;
uintptr_t maxilockpc;

struct
{
	uint64_t	locks;
	uint64_t	glare;
	uint64_t	inglare;
} lockstats;

#if 0
static void
dumplockmem(char *tag, Lock *l)
{
	uint8_t *cp;
	int i;

	iprint("%s: ", tag);
	cp = (uint8_t*)l;
	for(i = 0; i < 64; i++)
		iprint("%2.2ux ", cp[i]);
	iprint("\n");
}
#endif

void
lockloop(Lock *l, uintptr_t pc)
{
	Proc *p;

	p = l->lp;
	print("lock %#p loop key %#lux pc %#p held by pc %#p proc %lud\n",
		l, l->key, pc, l->pc, p ? p->pid : 0);
	dumpaproc(up);
	if(p != nil)
		dumpaproc(p);
}

int
lock(Lock *l)
{
	int i;
	uintptr_t pc;

	pc = getcallerpc();

	lockstats.locks++;
	if(up)
		up->nlocks++;	/* prevent being scheded */
	if(tas32(&l->key) == 0){
		if(up)
			up->lastlock = l;
		l->pc = pc;
		l->lp = up;
		l->lm = MACHP(m->machno);
		l->isilock = 0;
#ifdef LOCKCYCLES
		l->lockcycles = -lcycles();
#endif
		return 0;
	}
	if(up)
		up->nlocks--;

	lockstats.glare++;
	for(;;){
		lockstats.inglare++;
		i = 0;
		while(l->key){
			if(i++ > 100000000){
				i = 0;
				lockloop(l, pc);
			}
		}
		if(up)
			up->nlocks++;
		if(tas32(&l->key) == 0){
			if(up)
				up->lastlock = l;
			l->pc = pc;
			l->lp = up;
			l->lm = MACHP(m->machno);
			l->isilock = 0;
#ifdef LOCKCYCLES
			l->lockcycles = -lcycles();
#endif
			return 1;
		}
		if(up)
			up->nlocks--;
	}
}

void
ilock(Lock *l)
{
	uint64_t x;
	uintptr_t pc;

	pc = getcallerpc();
	lockstats.locks++;

	x = splhi();
	if(tas32(&l->key) != 0){
		lockstats.glare++;
		/*
		 * Cannot also check l->pc, l->lm, or l->isilock here
		 * because they might just not be set yet, or
		 * (for pc and m) the lock might have just been unlocked.
		 */
		for(;;){
			lockstats.inglare++;
			splx(x);
			while(l->key)
				;
			x = splhi();
			if(tas32(&l->key) == 0)
				goto acquire;
		}
	}
acquire:
	m->ilockdepth++;
	if(up)
		up->lastilock = l;
	l->sr = x;
	l->pc = pc;
	l->lp = up;
	l->lm = MACHP(m->machno);
	l->isilock = 1;
#ifdef LOCKCYCLES
	l->lockcycles = -lcycles();
#endif
}

int
canlock(Lock *l)
{
	if(up)
		up->nlocks++;
	if(tas32(&l->key)){
		if(up)
			up->nlocks--;
		return 0;
	}

	if(up)
		up->lastlock = l;
	l->pc = getcallerpc();
	l->lp = up;
	l->lm = MACHP(m->machno);
	l->isilock = 0;
#ifdef LOCKCYCLES
	l->lockcycles = -lcycles();
#endif
	return 1;
}

void
unlock(Lock *l)
{
#ifdef LOCKCYCLES
	l->lockcycles += lcycles();
	cumlockcycles += l->lockcycles;
	if(l->lockcycles > maxlockcycles){
		maxlockcycles = l->lockcycles;
		maxlockpc = l->pc;
	}
#endif
	if(l->key == 0)
		print("unlock: not locked: pc %#p\n", getcallerpc());
	if(l->isilock)
		print("unlock of ilock: pc %#p, held by %#p\n", getcallerpc(), l->pc);
	if(l->lp != up)
		print("unlock: up changed: pc %#p, acquired at pc %#p, lock p %#p, unlock up %#p\n", getcallerpc(), l->pc, l->lp, up);
	l->lm = nil;
	coherence();
	l->key = 0;

	if(up && --up->nlocks == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

uintptr_t ilockpcs[0x100] = { [0xff] = 1 };
//static int n;

void
iunlock(Lock *l)
{
	uint64_t sr;

#ifdef LOCKCYCLES
	l->lockcycles += lcycles();
	cumilockcycles += l->lockcycles;
	if(l->lockcycles > maxilockcycles){
		maxilockcycles = l->lockcycles;
		maxilockpc = l->pc;
	}
	if(l->lockcycles > 2400)
		ilockpcs[n++ & 0xff]  = l->pc;
#endif
	if(l->key == 0)
		print("iunlock: not locked: pc %#p\n", getcallerpc());
	if(!l->isilock)
		print("iunlock of lock: pc %#p, held by %#p\n", getcallerpc(), l->pc);
	if(islo())
		print("iunlock while lo: pc %#p, held by %#p\n", getcallerpc(), l->pc);

	sr = l->sr;
	l->lm = nil;
	coherence();
	l->key = 0;
	m->ilockdepth--;
	if(up)
		up->lastilock = nil;
	splx(sr);
}
