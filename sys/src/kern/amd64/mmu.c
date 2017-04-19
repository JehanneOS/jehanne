#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "amd64.h"

//#undef DBG
//#define DBG(...)	jehanne_print(__VA_ARGS__)

#define PDMAP		(0xffffffffff800000ull)
#define PDPX(v)		PTLX((v), 2)
#define PDX(v)		PTLX((v), 1)
#define PTX(v)		PTLX((v), 0)

#define VMAP		(0xffffffffe0000000ull)
#define VMAPSZ		(256*MiB)

#define KSEG1PML4	(0xffff000000000000ull\
			|(PTLX(KSEG1, 3)<<(((3)*PTSHFT)+PGSHFT))\
			|(PTLX(KSEG1, 3)<<(((2)*PTSHFT)+PGSHFT))\
			|(PTLX(KSEG1, 3)<<(((1)*PTSHFT)+PGSHFT))\
			|(PTLX(KSEG1, 3)<<(((0)*PTSHFT)+PGSHFT)))

#define KSEG1PTP(va, l)	((0xffff000000000000ull\
			|(KSEG1PML4<<((3-(l))*PTSHFT))\
			|(((va) & 0xffffffffffffull)>>(((l)+1)*PTSHFT))\
			& ~0xfffull))

static Lock vmaplock;
static Ptpage mach0pml4;
static struct{
	Lock;
	Ptpage*	next;
} ptpfreelist;
int	ptpcount;

#ifdef DO_mmuptpcheck
static void mmuptpcheck(Proc*);
#endif

void
mmuflushtlb(uint64_t _1)
{
	if(m->pml4->ptoff){
		jehanne_memset(m->pml4->pte, 0, m->pml4->ptoff*sizeof(PTE));
		m->pml4->ptoff = 0;
	}
	cr3put(m->pml4->pa);
}

void
mmuflush(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

static void
mmuptpfree(Proc* proc, int clear)
{
	int l;
	PTE *pte;
	Ptpage **last, *page;

	for(l = 0; l < 4; l++){
		last = &proc->mmuptp[l];
		if(*last == nil)
			continue;
		for(; (page = *last) != nil; last = &page->next){
			if(l <= 2 && clear)
				jehanne_memset(page->pte, 0, PTSZ);
			pte = page->parent->pte;
			pte[page->ptoff] = 0;
			proc->nptpbusy--;
		}
		*last = proc->ptpfree;
		proc->ptpfree = proc->mmuptp[l];
		proc->mmuptp[l] = nil;
	}

	m->pml4->ptoff = 0;
}

static Ptpage*
mmuptpalloc(void)
{
	Ptpage *page;
	uintmem pa;

	page = m->ptpfree;
	if(page != nil){
		m->ptpfree = page->next;
		m->nptpfree--;
	}else{
		lock(&ptpfreelist);
		page = ptpfreelist.next;
		if(page != nil)
			ptpfreelist.next = page->next;
		unlock(&ptpfreelist);
	}
	if(page != nil){
		page->next = nil;
		jehanne_memset(page->pte, 0, PTSZ);
		return page;
	}
	pa = physalloc(PTSZ);
	if(pa == 0){
		physdump();
		panic("mmuptpalloc");
	}
	DBG("ptp %#P\n", pa);
	page = jehanne_mallocz(sizeof(*page), 0);
	if(page == nil)
		panic("mmuptpalloc 2");
	page->pte = KADDR(pa);
	page->pa = pa;
	page->next = nil;
	page->parent = nil;
	page->ptoff = 0;
	jehanne_memset(page->pte, 0, PTSZ);
	return page;
}

void
mmuswitch(Proc* proc)
{
	PTE *pte;
	Ptpage *page;

	if(proc->newtlb){
		mmuptpfree(proc, 1);
		proc->newtlb = 0;
	}

	if(m->pml4->ptoff){
		jehanne_memset(m->pml4->pte, 0, m->pml4->ptoff*sizeof(PTE));
		m->pml4->ptoff = 0;
	}

	/* install new page directory pointers in pml4 */
	pte = m->pml4->pte;
	for(page = proc->mmuptp[3]; page != nil; page = page->next){
		pte[page->ptoff] = PPN(page->pa)|PteU|PteRW|PteP;
		if(page->ptoff >= m->pml4->ptoff)
			m->pml4->ptoff = page->ptoff+1;
		page->parent = m->pml4;
	}

	tssrsp0(STACKALIGN(PTR2UINT(proc->kstack+KSTACK)));
	cr3put(m->pml4->pa);
}

void
mmurelease(Proc* proc)
{
	Ptpage *page, **last;

	mmuptpfree(proc, 0);

	/* keep a few page tree pages per cpu */
	while((page = proc->ptpfree) != nil){
		page->parent = nil;
		if(sys->nmach != 1 && m->nptpfree > 20)
			break;
		proc->ptpfree = page->next;
		page->next = m->ptpfree;
		m->ptpfree = page;
		m->nptpfree++;
	}

	if(proc->ptpfree != nil){
		/* add the rest to the global pool */
		for(last = &proc->ptpfree; (page = *last) != nil; last = &page->next)
			page->parent = nil;
		lock(&ptpfreelist);
		*last = ptpfreelist.next;
		ptpfreelist.next = proc->ptpfree;
		proc->ptpfree = nil;
		unlock(&ptpfreelist);
	}

	if(proc->nptpbusy)
		jehanne_print("%ud: ptpbusy %s %d\n", proc->pid, proc->text, proc->nptpbusy);
	proc->nptpbusy = 0;

	tssrsp0(STACKALIGN(m->stack+MACHSTKSZ));
	cr3put(m->pml4->pa);
}

static PTE*
mmuptpget(uintptr_t va, int level)
{
	return (PTE*)KSEG1PTP(va, level);
}

static Ptpage*
makeptp(Ptpage *parent, int l, PTE *ptp, int x)
{
	Ptpage *page;
	PTE *pte;

	for(page = up->mmuptp[l]; page != nil; page = page->next)
		if(page->parent == parent && page->ptoff == x)
			return page;
	pte = &ptp[x];
	if(up->ptpfree == nil){
		page = mmuptpalloc();
	}
	else {
		page = up->ptpfree;
		up->ptpfree = page->next;
	}
	page->ptoff = x;
	page->next = up->mmuptp[l];
	up->mmuptp[l] = page;
	page->parent = parent;
	*pte = page->pa|PteU|PteRW|PteP;
	if(l == 3 && x >= m->pml4->ptoff)
		m->pml4->ptoff = x+1;
	up->nptpbusy++;
	DBG("%d: pte put l%d %#p[%d] -> %#P pte %#p\n", up->pid, l, ptp, x, *pte, pte);
	return page;
}

void
mmuput(uintptr_t va, uintptr_t pa)
{
	Mpl pl;
	int l, x, tl;
	PTE *pte, *ptp;
	Ptpage *prev;
	uint32_t attr;

	tl = (PGSHFT-12)/9;
	pl = splhi();
#ifdef DO_mmuptpcheck
	mmuptpcheck(up);
#endif
	for(l = 3; l != tl; l--){
		ptp = mmuptpget(va, l);
		pte = &ptp[PTLX(va,l)];
		if(l == tl)
			break;
		if((*pte & PteP) == 0 || *pte & PtePS)
			break;
	}
	if(l != tl){
		/* add missing intermediate level */
		prev = m->pml4;
		for(l = 3; l > tl; l--){
			ptp = mmuptpget(va, l);
			x = PTLX(va, l);
			prev = makeptp(prev, l, ptp, x);
		}
	}
	ptp = mmuptpget(va, tl);
	pte = &ptp[PTLX(va, tl)];
	attr = 0;
	if(tl > 0)
		attr |= PtePS;
	*pte = pa | attr | PteU;
	DBG("%d mach%d: put pte %#p: %#p -> %#P\n", up->pid, m->machno, pte, va, *pte);

	/* Simple and safe: programs can either write memory or execute it.
	 * TODO: verify that this is not too restrictive
	 */
	if(pa & PteRW)
		*pte |= PteNX;

	splx(pl);
	invlpg(va);			/* only if old entry valid? */
}

static PTE
pdeget(uintptr_t va)
{
	PTE *pdp;

	if(va < 0xffffffffc0000000ull)
		panic("pdeget(%#p)", va);

	pdp = (PTE*)(PDMAP+PDX(PDMAP)*4096);

	return pdp[PDX(va)];
}

/*
 * Add kernel mappings for va -> pa for a section of size bytes.
 * Called only after the va range is known to be unoccupied.
 */
static int
pdmap(uintmem pa, int attr, uintptr_t va, usize size)
{
	uintmem pae;
	PTE *pd, *pde, *pt, *pte;
	uintmem pdpa;
	int pdx, pgsz;

	pd = (PTE*)(PDMAP+PDX(PDMAP)*4096);

	for(pae = pa + size; pa < pae; pa += pgsz){
		pdx = PDX(va);
		pde = &pd[pdx];

		/*
		 * Check if it can be mapped using a big page,
		 * i.e. is big enough and starts on a suitable boundary.
		 * Assume processor can do it.
		 */
		if(ALIGNED(pa, PGLSZ(1)) && ALIGNED(va, PGLSZ(1)) && (pae-pa) >= PGLSZ(1)){
			assert(*pde == 0);
			*pde = pa|attr|PtePS|PteP;
			pgsz = PGLSZ(1);
		}
		else{
			pt = (PTE*)(PDMAP+pdx*PTSZ);
			if(*pde == 0){
				pdpa = physalloc(PTSZ);
				if(pdpa == 0)
					panic("pdmap");
				*pde = pdpa|PteRW|PteP;
//jehanne_print("*pde %#llux va %#p\n", *pde, va);
				jehanne_memset(pt, 0, PTSZ);
			}

			pte = &pt[PTX(va)];
			assert(!(*pte & PteP));
			*pte = pa|attr|PteP;
			pgsz = PGLSZ(0);
		}
		va += pgsz;
	}

	return 0;
}

static int
findhole(PTE* a, int n, int count)
{
	int have, i;

	have = 0;
	for(i = 0; i < n; i++){
		if(a[i] == 0)
			have++;
		else
			have = 0;
		if(have >= count)
			return i+1 - have;
	}

	return -1;
}

/*
 * Look for free space in the vmap.
 */
static uintptr_t
vmapalloc(usize size)
{
	int i, n, o;
	PTE *pd, *pt;
	int pdsz, ptsz;

	pd = (PTE*)(PDMAP+PDX(PDMAP)*4096);
	pd += PDX(VMAP);
	pdsz = VMAPSZ/PGLSZ(1);

	/*
	 * Look directly in the PD entries if the size is
	 * larger than the range mapped by a single entry.
	 */
	if(size >= PGLSZ(1)){
		n = HOWMANY(size, PGLSZ(1));
		if((o = findhole(pd, pdsz, n)) != -1)
			return VMAP + o*PGLSZ(1);
		return 0;
	}

	/*
	 * Size is smaller than that mapped by a single PD entry.
	 * Look for an already mapped PT page that has room.
	 */
	n = HOWMANY(size, PGLSZ(0));
	ptsz = PGLSZ(0)/sizeof(PTE);
	for(i = 0; i < pdsz; i++){
		if(!(pd[i] & PteP) || (pd[i] & PtePS))
			continue;

		pt = (PTE*)(PDMAP+(PDX(VMAP)+i)*4096);
		if((o = findhole(pt, ptsz, n)) != -1)
			return VMAP + i*PGLSZ(1) + o*PGLSZ(0);
	}

	/*
	 * Nothing suitable, start using a new PD entry.
	 */
	if((o = findhole(pd, pdsz, 1)) != -1)
		return VMAP + o*PGLSZ(1);

	return 0;
}

void*
vmap(uintptr_t pa, usize size)
{
	uintptr_t va;
	usize o, sz;

	DBG("vmap(%#p, %lud)\n", pa, size);

	if(m->machno != 0)
		panic("vmap(%#p, %lud) pc %#p mach%d", pa, size, getcallerpc(), m->machno);

	/*
	 * This is incomplete; the checks are not comprehensive
	 * enough.
	 * Sometimes the request is for an already-mapped piece
	 * of low memory, in which case just return a good value
	 * and hope that a corresponding vunmap of the address
	 * will have the same address.
	 * To do this properly will require keeping track of the
	 * mappings; perhaps something like kmap, but kmap probably
	 * can't be used early enough for some of the uses.
	 */
	if(pa+size <= 1ull*MiB)
		return KADDR(pa);
	if(pa < 1ull*MiB)
		return nil;

	/*
	 * Might be asking for less than a page.
	 * This should have a smaller granularity if
	 * the page size is large.
	 */
	o = pa & ((1<<PGSHFT)-1);
	pa -= o;
	sz = ROUNDUP(size+o, PGSZ);

	if(pa == 0){
		jehanne_print("vmap(0, %lud) pc=%#p\n", size, getcallerpc());
		return nil;
	}
	ilock(&vmaplock);
	if((va = vmapalloc(sz)) == 0 || pdmap(pa, PtePCD|PteRW, va, sz) < 0){
		iunlock(&vmaplock);
		return nil;
	}
	iunlock(&vmaplock);

	DBG("vmap(%#p, %lud) => %#p\n", pa+o, size, va+o);

	return UINT2PTR(va + o);
}

void
vunmap(void* v, usize size)
{
	uintptr_t va;

	DBG("vunmap(%#p, %lud)\n", v, size);

	if(m->machno != 0)
		panic("vunmap");

	/*
	 * See the comments above in vmap.
	 */
	va = PTR2UINT(v);
	if(va >= KZERO && va+size < KZERO+1ull*MiB)
		return;

	/*
	 * Here will have to deal with releasing any
	 * resources used for the allocation (e.g. page table
	 * pages).
	 */
	DBG("vunmap(%#p, %lud)\n", v, size);
}

int
mmuwalk(uintptr_t va, int level, PTE** ret, uint64_t (*alloc)(usize))
{
	int l;
	Mpl pl;
	uintmem pa;
	PTE *pte, *ptp;

	DBG("mmuwalk%d: va %#p level %d\n", m->machno, va, level);
	pte = nil;
	pl = splhi();
	for(l = 3; l >= 0; l--){
		ptp = mmuptpget(va, l);
		pte = &ptp[PTLX(va, l)];
		if(l == level)
			break;
		if(!(*pte & PteP)){
			if(alloc == nil)
				return -1;
			pa = alloc(PTSZ);
			if(pa == ~(uintmem)0 || pa == 0)
				return -1;
if(pa & 0xfffull) jehanne_print("mmuwalk pa %#llux\n", pa);
			*pte = pa|PteRW|PteP;
			if((ptp = mmuptpget(va, l-1)) == nil)
				panic("mmuwalk: mmuptpget(%#p, %d)", va, l-1);
			jehanne_memset(ptp, 0, PTSZ);
		}
		else if(*pte & PtePS)
			break;
	}
	*ret = pte;
	splx(pl);

	return l;
}

uint64_t
mmuphysaddr(uintptr_t va)
{
	int l;
	PTE *pte;
	uint64_t mask, pa;

	/*
	 * Given a VA, find the PA.
	 * This is probably not the right interface,
	 * but will do as an experiment. Usual
	 * question, should va be void* or uintptr_t?
	 */
	l = mmuwalk(va, 0, &pte, nil);
	DBG("physaddr: va %#p l %d\n", va, l);
	if(l < 0 || (*pte & PteP) == 0)
		return ~(uintmem)0;

	mask = (1ull<<(((l)*PTSHFT)+PGSHFT))-1;
	pa = (*pte & ~mask) + (va & mask);

	DBG("physaddr: l %d va %#p pa %#llux\n", l, va, pa);

	return pa;
}

void
mmuinit(void)
{
	int l;
	uint8_t *p;
	PTE *pte;
	Ptpage *page;
	uintptr_t pml4;
	uint64_t o, pa, r, sz;

	archmmu();
	DBG("mach%d: %#p npgsz %d\n", m->machno, m, m->npgsz);
	if(m->machno != 0){
		/*
		 * GAK: Has to go when each mach is using
		 * its own page table
		 */
		p = UINT2PTR(m->stack);
		p += MACHSTKSZ;
		jehanne_memmove(p, mach0pml4.pte, PTSZ);
		m->pml4 = &m->pml4kludge;
		m->pml4->pte = (PTE*)p;
		m->pml4->pa = PADDR(p);
		m->pml4->ptoff = mach0pml4.ptoff;	/* # of user mappings in pml4 */
		if(m->pml4->ptoff){
			jehanne_memset(p, 0, m->pml4->ptoff*sizeof(PTE));
			m->pml4->ptoff = 0;
		}
pte = (PTE*)p;
pte[PTLX(KSEG1PML4, 3)] = m->pml4->pa|PteRW|PteP;

		r = rdmsr(Efer);
		r |= Nxe;
		wrmsr(Efer, r);
		cr3put(m->pml4->pa);
		DBG("mach%d: %#p pml4 %#p\n", m->machno, m, m->pml4);
		return;
	}

	page = &mach0pml4;
	page->pa = cr3get();
	page->pte = sys->pml4;

	m->pml4 = page;

	r = rdmsr(Efer);
	r |= Nxe;
	wrmsr(Efer, r);

	/*
	 * Set up the various kernel memory allocator limits:
	 * pmstart/pmend bound the unused physical memory;
	 * vmstart/vmunmapped bound the total possible virtual memory
	 * used by the kernel in KSEG0;
	 * vmunused is the highest virtual address currently mapped
	 * and used by the kernel;
	 * vmunmapped is the highest virtual address currently
	 * mapped by the kernel.
	 * Vmunused can be bumped up to vmunmapped before more
	 * physical memory needs to be allocated and mapped.
	 *
	 * This is set up here so meminit can map appropriately.
	 */
	o = sys->pmstart;
	sz = ROUNDUP(o+128*KiB, 4*MiB) - o;	/* add extra 128k for initial pt/pd allocations */
jehanne_print("mmuinit: rmapalloc: %#P pmstart=%#llux\n", o, sys->pmstart);
	pa = rmapalloc(&rmapram, o, sz, 0);
	if(pa != o)
		panic("mmuinit: pa %#llux memstart %#llux\n", pa, o);
	sys->pmstart += sz;

	sys->vmstart = KSEG0;
	sys->vmunused = sys->vmstart + ROUNDUP(o, 4*KiB);
	sys->vmunmapped = sys->vmstart + o + sz;

	jehanne_print("mmuinit: vmstart %#p vmunused %#p vmunmapped %#p\n",
		sys->vmstart, sys->vmunused, sys->vmunmapped);

	/*
	 * Set up the map for PD entry access by inserting
	 * the relevant PDP entry into the PD. It's equivalent
	 * to PADDR(sys->pd)|PteRW|PteP.
	 *
	 * Change code that uses this to use the KSEG1PML4
	 * map below.
	 */
	sys->pd[PDX(PDMAP)] = sys->pdp[PDPX(PDMAP)] & ~(PteD|PteA);
	jehanne_print("sys->pd %#p %#p\n", sys->pd[PDX(PDMAP)], sys->pdp[PDPX(PDMAP)]);

	assert((pdeget(PDMAP) & ~(PteD|PteA)) == (PADDR(sys->pd)|PteRW|PteP));

	/*
	 * Set up the map for PTE access by inserting
	 * the relevant PML4 into itself.
	 * Note: outwith level 0, PteG is MBZ on AMD processors,
	 * is 'Reserved' on Intel processors, and the behaviour
	 * can be different.
	 */
	pml4 = cr3get();
	sys->pml4[PTLX(KSEG1PML4, 3)] = pml4|PteRW|PteP;
	cr3put(m->pml4->pa);

	if((l = mmuwalk(KZERO, 3, &pte, nil)) >= 0)
		jehanne_print("l %d %#p %llux\n", l, pte, *pte);
	if((l = mmuwalk(KZERO, 2, &pte, nil)) >= 0)
		jehanne_print("l %d %#p %llux\n", l, pte, *pte);
	if((l = mmuwalk(KZERO, 1, &pte, nil)) >= 0)
		jehanne_print("l %d %#p %llux\n", l, pte, *pte);
	if((l = mmuwalk(KZERO, 0, &pte, nil)) >= 0)
		jehanne_print("l %d %#p %llux\n", l, pte, *pte);

	mmuphysaddr(PTR2UINT(end));
}

void
mmudump(Proc *p)
{
	Ptpage *ptp;
	int i, l;

	for(l = 3; l >= 0; l--){
		for(ptp = p->mmuptp[l]; ptp != nil; ptp = ptp->next){
			jehanne_print("pid %d level %d ptp %#p\n", p->pid, l, ptp);
			for(i = 0; i < PTSZ/sizeof(PTE); i++)
				if(ptp->pte[i])
					jehanne_print("%.4d %#P\n", i, ptp->pte[i]);
		}
	}
}

/*
 * Double-check the user MMU.
 * Error checking only.
 */
void
checkmmu(uintptr_t va, uintmem pa)
{
	uintmem mpa;

	mpa = mmuphysaddr(va);
	if(mpa != ~(uintmem)0 && (mpa & (PteNX-1)) != pa)
		jehanne_print("***%d %s: mmu mismatch va=%#p pa=%#P mmupa=%#P\n",
			up->pid, up->text, va, pa, mpa);
}

static void
tabs(int n)
{
	int i;

	for(i = 0; i < n; i++)
		jehanne_print("  ");
}

void
dumpptepg(int lvl, uintmem pa)
{
	PTE *pte;
	int tab, i;

	tab = 4 - lvl;
	pte = UINT2PTR(KADDR(pa));
	for(i = 0; i < PTSZ/sizeof(PTE); i++)
		if(pte[i] & PteP){
			tabs(tab);
			jehanne_print("l%d %#p[%#05x]: %#llux\n", lvl, pa, i, pte[i]);

			/* skip kernel mappings */
			if((pte[i]&PteU) == 0){
				tabs(tab+1);
				jehanne_print("...kern...\n");
				continue;
			}
			if(lvl > 2)
				dumpptepg(lvl-1, PPN(pte[i]));
		}
}

void
dumpmmu(Proc *p)
{
	int i;
	Ptpage *pt;

	jehanne_print("proc %#p\n", p);
	for(i = 3; i >= 0; i--){
		jehanne_print("mmuptp[%d]:\n", i);
		for(pt = p->mmuptp[i]; pt != nil; pt = pt->next)
			jehanne_print("\tpt %#p = va %#p pa %#P"
				" ptoff %#ux next %#p parent %#p\n",
				pt, pt->pte, pt->pa, pt->ptoff, pt->next, pt->parent);
	}
	jehanne_print("pml4 %#P\n", m->pml4->pa);
	if(0)dumpptepg(4, m->pml4->pa);
}

void
dumpmmuwalk(uintmem addr)
{
	int l;
	PTE *pte;

	if((l = mmuwalk(addr, 3, &pte, nil)) >= 0)
		jehanne_print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(addr, 2, &pte, nil)) >= 0)
		jehanne_print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(addr, 1, &pte, nil)) >= 0)
		jehanne_print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(addr, 0, &pte, nil)) >= 0)
		jehanne_print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
}
#ifdef DO_mmuptpcheck
static void
mmuptpcheck(Proc *proc)
{
	int lvl, npgs, i;
	Mpl pl;
	Ptpage *lp, *p, **pgs, *fp;
	enum{Tsize = 512};
	static Ptpage *pgtab[MACHMAX][Tsize];
	static uint32_t idxtab[MACHMAX][Tsize];
	uint32_t *idx;

	if(proc == nil)
		return;
	pl = splhi();
	pgs = pgtab[m->machno];
	idx = idxtab[m->machno];
	lp = m->pml4;
	for(lvl = 3; lvl >= 1; lvl--){
		npgs = 0;
		for(p = proc->mmuptp[lvl]; p != nil; p = p->next){
			for(fp = proc->ptpfree; fp != nil; fp = fp->next)
				if(fp == p){
					dumpmmu(proc);
					panic("ptpcheck: using free page");
				}
			for(i = 0; i < npgs; i++){
				if(pgs[i] == p){
					dumpmmu(proc);
					panic("ptpcheck: dup page");
				}
				if(idx[i] == p->ptoff){
					dumpmmu(proc);
					panic("ptcheck: dup daddr");
				}
			}
			if(npgs >= Tsize)
				panic("ptpcheck: pgs is too small");
			idx[npgs] = p->ptoff;
			pgs[npgs++] = p;
			if(lvl == 3 && p->parent != lp){
				dumpmmu(proc);
				panic("ptpcheck: wrong parent");
			}
		}
		
	}
	npgs = 0;
	for(fp = proc->ptpfree; fp != nil; fp = fp->next){
		for(i = 0; i < npgs; i++)
			if(pgs[i] == fp)
				panic("ptpcheck: dup free page");
		pgs[npgs++] = fp;
	}
	splx(pl);
}
#endif
