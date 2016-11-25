#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * TO DO:
 *	big pool -> small pool
 */

enum{
	MinK=		PGSHFT,
	MidK=		21,
	MaxK=		30,		/* last usable k (largest block is 2^k) */
};

static	Bpool*	physgig;
static	Bpool*	phys;

static void*
alloc0(usize nb, int clr)
{
	void *p;

	p = basealloc(nb, 0, nil);
	if(clr && p != nil)
		memset(p, 0, nb);
	return p;
}

uintmem
physalloc(usize size)
{
	uintmem a;

	if(physgig != nil && size >= 1ull<<MidK){
		a = bpoolalloc(physgig, size);
		if(a != 0)
			return a;
	}
	return bpoolalloc(phys, size);
}

void
physfree(uintmem a, usize size)
{
	if(physgig != nil && a >= 4ull*GiB)
		bpoolfree(physgig, a, size);
	else
		bpoolfree(phys, a, size);
}

void
physallocrange(usize *low, usize *high)
{
	bpoolallocrange(phys, low, high);
}

void
physinitfree(uintmem base, uintmem lim)
{
	uintmem a, lo, hi;

	base = ROUNDUP(base, (1<<MinK));
	lim = ROUNDDN(lim, (1<<MinK));
	if(base == lim)
		return;
	if(physgig != nil && (base >= 4ull*GiB || lim > 4ull*GiB)){
		a = base;
		if(a < 4ull*GiB)
			a = 4ull*GiB;
		lo = ROUNDUP(a, (1<<MidK));
		hi = ROUNDDN(lim, (1<<MidK));
		if(lo != hi)
			bpoolinitfree(physgig, lo, hi);
		if(base == a)
			return;
		lim = 4ull*GiB;
	}
	bpoolinitfree(phys, base, lim);
}

char*
seprintphysstats(char *s,  char *e)
{
	s = seprintbpoolstats(phys, s, e);
	if(physgig != nil)
		s = seprintbpoolstats(physgig, s, e);
	return s;
}

void
physallocinit(void)
{
	uintmem top, avail, base, size, lim, pa, lo, hi;
	RMapel *e;

	if(DBGFLG)
		rmapprint(&rmapram);
	avail = rmapsize(&rmapram);
	DBG("avail: %#P\n", avail);
	top = 0;
	for(e = rmapram.map; e != nil; e = e->next)
		top = e->addr + e->size;
	if(top > 4ull*GiB){
		physgig = bpoolcreate(MidK, MaxK, 4ull*GiB, top, alloc0);
		phys = bpoolcreate(MinK, MaxK, 0, 4ull*GiB, alloc0);
	}else
		phys = bpoolcreate(MinK, MaxK, 0, top, alloc0);
	pa = mmuphysaddr(sys->vmstart) + sys->pmunassigned;
	if(DBGFLG)
		rmapprint(&rmapram);
	DBG("pa lim: %#llux top %#llux\n", pa, top);
	while(rmapfirst(&rmapram, pa, &base, &size)){
		if(base >= 4ull*GiB)
			break;
		lim = base+size;
		if(lim > 4ull*GiB)
			lim = 4ull*GiB;
		lo = ROUNDUP(base, (1<<MinK));
		hi = ROUNDDN(lim, (1<<MinK));
		if(lo != hi){
			DBG("lo=%#llux hi=%#llux\n", lo, hi);
			pa = rmapalloc(&rmapram, lo, hi-lo, 0);
			if(pa == 0)
				break;
			physinitfree(lo, hi);
			sys->pmpaged += hi - lo;
		}
	}
	if(DBGFLG)
		physdump();
}

void
physdump(void)
{
	print("bpooldump phys: ");
	bpooldump(phys);
	if(physgig != nil){
		print("bpooldump physgig: ");
		bpooldump(physgig);
	}
}
