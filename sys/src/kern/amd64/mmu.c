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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"amd64.h"

/*
 * Simple segment descriptors with no translation.
 */
#define	EXECSEGM(p) 	{ 0, SEGL|SEGP|SEGPL(p)|SEGEXEC }
#define	DATASEGM(p) 	{ 0, SEGB|SEGG|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	EXEC32SEGM(p) 	{ 0xFFFF, SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	DATA32SEGM(p) 	{ 0xFFFF, SEGB|SEGG|(0xF<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }

Segdesc gdt[NGDT] =
{
[NULLSEG]	{ 0, 0},		/* null descriptor */
[KESEG]		EXECSEGM(0),		/* kernel code */
[KDSEG]		DATASEGM(0),		/* kernel data */
[UE32SEG]	EXEC32SEGM(3),		/* user code 32 bit*/
[UDSEG]		DATA32SEGM(3),		/* user data/stack */
[UESEG]		EXECSEGM(3),		/* user code */
};

static struct {
	Lock;
	MMU	*free;

	unsigned long	nalloc;
	unsigned long	nfree;
} mmupool;

enum {
	/* level */
	PML4E	= 2,
	PDPE	= 1,
	PDE	= 0,

	MAPBITS	= 8*sizeof(m->mmumap[0]),

	/* PAT entry used for write combining */
	PATWC	= 7,
};

#if 0
static void
loadptr(uint16_t lim, uintptr_t off, void (*load)(void*))
{
	uint64_t b[2], *o;
	uint16_t *s;

	o = &b[1];
	s = ((uint16_t*)o)-1;

	*s = lim;
	*o = off;

	(*load)(s);
}
#endif

static void
taskswitch(uintptr_t stack)
{
	Tss *tss;

	tss = m->tss;
	tss->rsp0[0] = (uint32_t)stack;
	tss->rsp0[1] = stack >> 32;
	tss->rsp1[0] = (uint32_t)stack;
	tss->rsp1[1] = stack >> 32;
	tss->rsp2[0] = (uint32_t)stack;
	tss->rsp2[1] = stack >> 32;
	mmuflushtlb();
}

void
mmuinit(void)
{
	uintptr_t x;
	long v;
	int i;

	/* zap double map done by entry.S */
	m->pml4[512] = 0;
	m->pml4[0] = 0;

	m->tss = mallocz(sizeof(Tss), 1);
	if(m->tss == nil)
		panic("mmuinit: no memory for Tss");
	m->tss->iomap = 0xDFFF;
	for(i=0; i<14; i+=2){
		x = (uintptr_t)m + MACHSIZE;
		m->tss->ist[i] = x;
		m->tss->ist[i+1] = x>>32;
	}

	/*
	 * We used to keep the GDT in the Mach structure, but it
	 * turns out that that slows down access to the rest of the
	 * page.  Since the Mach structure is accessed quite often,
	 * it pays off anywhere from a factor of 1.25 to 2 on real
	 * hardware to separate them (the AMDs are more sensitive
	 * than Intels in this regard).  Under VMware it pays off
	 * a factor of about 10 to 100.
	 */
	memmove(m->gdt, gdt, sizeof gdt);

	x = (uintptr_t)m->tss;
	m->gdt[TSSSEG+0].d0 = (x<<16)|(sizeof(Tss)-1);
	m->gdt[TSSSEG+0].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;
	m->gdt[TSSSEG+1].d0 = x>>32;
	m->gdt[TSSSEG+1].d1 = 0;

	gdtput(sizeof(gdt)-1, PTR2UINT(m->gdt), SSEL(SiCS, SsTIGDT|SsRPL0));
	idtput(sizeof(Segdesc)*512-1, (uintptr_t)IDTADDR);
//	trput(SSEL(SiTSS, SsTIGDT|SsRPL0));

//	loadptr(sizeof(gdt)-1, (uintptr_t)m->gdt, lgdt);
//	loadptr(sizeof(Segdesc)*512-1, (uintptr_t)IDTADDR, lidt);
	taskswitch((uintptr_t)m + MACHSIZE);
	trput(TSSSEL);

	wrmsr(0xc0000100, 0ull);	/* 64 bit fsbase */
	wrmsr(0xc0000101, (unsigned long)&sys->machptr[m->machno]);	/* 64 bit gsbase */
	wrmsr(0xc0000102, 0ull);	/* kernel gs base */

	/* enable syscall extension */
	rdmsr(0xc0000080, &v);
	v |= 1ull;
	wrmsr(0xc0000080, v);

	/* enable no execute */
	rdmsr(0xc0000080, &v);
	v |= Nxe;
	wrmsr(0xc0000080, v);

	/* IA32_STAR */
	wrmsr(0xc0000081, ((unsigned long)UE32SEL << 48) | ((unsigned long)KESEL << 32));

	/* IA32_LSTAR */
	wrmsr(0xc0000082, (unsigned long)syscallentry);

	/* SYSCALL flags mask */
	wrmsr(0xc0000084, 0x200);

	/* IA32_PAT write combining */
	if((MACHP(0)->cpuiddx & Pat) != 0
	&& rdmsr(0x277, &v) != -1){
		v &= ~(255LL<<(PATWC*8));
		v |= 1LL<<(PATWC*8);	/* WC */
		wrmsr(0x277, v);
	}
}

/*
 * These could go back to being macros once the kernel is debugged,
 * but the extra checking is nice to have.
 */
void*
mmu_kernel_address(uintptr_t pa)
{
	if(pa >= (uintptr_t)-KZERO)
		panic("mmu_kernel_address: pa=%#p pc=%#p", pa, getcallerpc());
	return (void*)(pa+KZERO);
}

uintptr_t
mmu_physical_address(void *v)
{
	uintptr_t va;

	va = (uintptr_t)v;
	if(va >= KZERO)
		return va-KZERO;
	if(va >= VMAP)
		return va-VMAP;
	panic("mmu_physical_address: va=%#p pc=%#p", va, getcallerpc());
	return 0;
}

static MMU*
mmualloc(void)
{
	MMU *p;
	int i, n;

	p = m->mmufree;
	if(p != nil){
		m->mmufree = p->next;
		m->mmucount--;
	} else {
		lock(&mmupool);
		p = mmupool.free;
		if(p != nil){
			mmupool.free = p->next;
			mmupool.nfree--;
		} else {
			unlock(&mmupool);

			n = 256;
			p = malloc(n * sizeof(MMU));
			if(p == nil)
				panic("mmualloc: out of memory for MMU");
			p->page = mallocalign(n * PTSZ, PGSZ, 0, 0);
			if(p->page == nil)
				panic("mmualloc: out of memory for MMU pages");
			for(i=1; i<n; i++){
				p[i].page = p[i-1].page + (1<<PTSHFT);
				p[i-1].next = &p[i];
			}

			lock(&mmupool);
			p[n-1].next = mmupool.free;
			mmupool.free = p->next;
			mmupool.nalloc += n;
			mmupool.nfree += n-1;
		}
		unlock(&mmupool);
	}
	p->next = nil;
	return p;
}

static uintptr_t*
mmucreate(uintptr_t *table, uintptr_t va, int level, int index)
{
	uintptr_t *page, flags;
	MMU *p;

	flags = PTEWRITE|PTEVALID;
	if(va < VMAP){
		assert(up != nil);
		assert((va < TSTKTOP) || (va >= KMAP && va < KMAP+KMAPSIZE));

		p = mmualloc();
		p->index = index;
		p->level = level;
		if(va < TSTKTOP){
			flags |= PTEUSER;
			if(level == PML4E){
				if((p->next = up->mmuhead) == nil)
					up->mmutail = p;
				up->mmuhead = p;
				m->mmumap[index/MAPBITS] |= 1ull<<(index%MAPBITS);
			} else {
				up->mmutail->next = p;
				up->mmutail = p;
			}
			up->mmucount++;
		} else {
			if(level == PML4E){
				up->kmaptail = p;
				up->kmaphead = p;
			} else {
				up->kmaptail->next = p;
				up->kmaptail = p;
			}
			up->kmapcount++;
		}
		page = p->page;
	} else if(sys->mem[0].npage != 0) {
		page = mallocalign(PTSZ, PGSZ, 0, 0);
	} else {
		page = rampage();
	}
	memset(page, 0, PTSZ);
	table[index] = PADDR(page) | flags;
	return page;
}

uintptr_t*
mmuwalk(uintptr_t* table, uintptr_t va, int level, int create)
{
	uintptr_t pte;
	int i, x;

	x = PTLX(va, 3);
	for(i = 2; i >= level; i--){
		pte = table[x];
		if(pte & PTEVALID){
			if(pte & PTESIZE)
				return 0;
			table = KADDR(PPN(pte));
		} else {
			if(!create)
				return 0;
			table = mmucreate(table, va, i, x);
		}
		x = PTLX(va, i);
	}
	return &table[x];
}

static int
ptecount(uintptr_t va, int level)
{
	return (1<<PTSHFT) - (va & PGLSZ(level+1)-1) / PGLSZ(level);
}

void
pmap(uintptr_t *pml4, uintptr_t pa, uintptr_t va, long size)
{
	uintptr_t *pte, *ptee, flags;
	int z, l;

	if(size <= 0 || va < VMAP)
		panic("pmap: pa=%#p va=%#p size=%lld", pa, va, size);
	flags = pa;
	pa = PPN(pa);
	flags -= pa;
	if(va >= KZERO)
		flags |= PTEGLOBAL;
	while(size > 0){
		if(size >= PGLSZ(1) && (va % PGLSZ(1)) == 0)
			flags |= PTESIZE;
		l = (flags & PTESIZE) != 0;
		z = PGLSZ(l);
		pte = mmuwalk(pml4, va, l, 1);
		if(pte == 0){
			pte = mmuwalk(pml4, va, ++l, 0);
			if(pte && (*pte & PTESIZE)){
				flags |= PTESIZE;
				z = va & (PGLSZ(l)-1);
				va -= z;
				pa -= z;
				size += z;
				continue;
			}
			panic("pmap: pa=%#p va=%#p size=%lld", pa, va, size);
		}
		ptee = pte + ptecount(va, l);
		while(size > 0 && pte < ptee){
			*pte++ = pa | flags;
			pa += z;
			va += z;
			size -= z;
		}
	}
}

static void
mmuzap(void)
{
	uintptr_t *pte;
	uint64_t w;
	int i, x;

	pte = m->pml4;
	pte[PTLX(KMAP, 3)] = 0;

	/* common case */
	pte[PTLX(UTZERO, 3)] = 0;
	pte[PTLX(TSTKTOP, 3)] = 0;
	m->mmumap[PTLX(UTZERO, 3)/MAPBITS] &= ~(1ull<<(PTLX(UTZERO, 3)%MAPBITS));
	m->mmumap[PTLX(TSTKTOP, 3)/MAPBITS] &= ~(1ull<<(PTLX(TSTKTOP, 3)%MAPBITS));

	for(i = 0; i < nelem(m->mmumap); pte += MAPBITS, i++){
		if((w = m->mmumap[i]) == 0)
			continue;
		m->mmumap[i] = 0;
		for(x = 0; w != 0; w >>= 1, x++){
			if(w & 1)
				pte[x] = 0;
		}
	}
}

static void
mmufree(Proc *proc)
{
	MMU *p;

	p = proc->mmutail;
	if(p == nil)
		return;
	if(m->mmucount+proc->mmucount < 256){
		p->next = m->mmufree;
		m->mmufree = proc->mmuhead;
		m->mmucount += proc->mmucount;
	} else {
		lock(&mmupool);
		p->next = mmupool.free;
		mmupool.free = proc->mmuhead;
		mmupool.nfree += proc->mmucount;
		unlock(&mmupool);
	}
	proc->mmuhead = proc->mmutail = nil;
	proc->mmucount = 0;
}

void
mmuflush(void)
{
	int x;

	x = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(x);
}

void
mmuswitch(Proc *proc)
{
	MMU *p;

	mmuzap();
	if(proc->newtlb){
		mmufree(proc);
		proc->newtlb = 0;
	}
	if((p = proc->kmaphead) != nil)
		m->pml4[PTLX(KMAP, 3)] = PADDR(p->page) | PTEWRITE|PTEVALID;
	for(p = proc->mmuhead; p != nil && p->level == PML4E; p = p->next){
		m->mmumap[p->index/MAPBITS] |= 1ull<<(p->index%MAPBITS);
		m->pml4[p->index] = PADDR(p->page) | PTEUSER|PTEWRITE|PTEVALID;
	}
	taskswitch((uintptr_t)proc->kstack+KSTACK);
}

void
mmurelease(Proc *proc)
{
	MMU *p;

	mmuzap();
	if((p = proc->kmaptail) != nil){
		if((p->next = proc->mmuhead) == nil)
			proc->mmutail = p;
		proc->mmuhead = proc->kmaphead;
		proc->mmucount += proc->kmapcount;

		proc->kmaphead = proc->kmaptail = nil;
		proc->kmapcount = proc->kmapindex = 0;
	}
	mmufree(proc);
	taskswitch((uintptr_t)m+MACHSIZE);
}

void
mmuput(uintptr_t va, uintptr_t pa)
{
	uintptr_t *pte, old;
	int x;

	x = splhi();
	pte = mmuwalk(m->pml4, va, 0, 1);
	if(pte == 0)
		panic("mmuput: bug: va=%#p pa=%#p", va, pa);
	old = *pte;
	if(pa & PteRW)
		pa |= PteNX;
	*pte = pa | PTEVALID|PTEUSER;
	splx(x);
	if(old & PTEVALID)
		invlpg(va);
}

/*
 * Double-check the user MMU.
 * Error checking only.
 */
void
checkmmu(uintptr_t va, uintptr_t pa)
{
	uintptr_t *pte;

	pte = mmuwalk(m->pml4, va, 0, 0);
	if(pte != 0 && (*pte & PTEVALID) != 0 && PPN((*pte & (PteNX-1))) != pa)
		print("%ld %s: va=%#p pa=%#p pte=%#p\n",
			up->pid, up->text, va, pa, *pte);
}

uintptr_t
cankaddr(uintptr_t pa)
{
	if(pa >= -KZERO)
		return 0;
	return -KZERO - pa;
}

void
countpagerefs(unsigned long *ref, int print)
{
	USED(ref, print);
}

KMap*
kmap(uintptr_t pa)
{
	uintptr_t *pte, va;
	int x;

	if(cankaddr(pa) != 0)
		return (KMap*)KADDR(pa);

	x = splhi();
	va = KMAP + ((uintptr_t)up->kmapindex << PGSHFT);
	pte = mmuwalk(m->pml4, va, 0, 1);
	if(pte == 0 || *pte & PTEVALID)
		panic("kmap: pa=%#p va=%#p", pa, va);
	*pte = pa | PTEWRITE|PTEVALID;
	up->kmapindex = (up->kmapindex + 1) % (1<<PTSHFT);
	if(up->kmapindex == 0)
		mmuflushtlb();
	splx(x);
	return (KMap*)va;
}

void
kunmap(KMap *k)
{
	uintptr_t *pte, va;
	int x;

	va = (uintptr_t)k;
	if(va >= KZERO)
		return;

	x = splhi();
	pte = mmuwalk(m->pml4, va, 0, 0);
	if(pte == 0 || (*pte & PTEVALID) == 0)
		panic("kunmap: va=%#p", va);
	*pte = 0;
	splx(x);
}

uint64_t
mmuphysaddr(uintptr_t va)
{
	uintptr_t *pte;
	uint64_t mask, pa;

	/*
	 * Given a VA, find the PA.
	 * This is probably not the right interface,
	 * but will do as an experiment. Usual
	 * question, should va be void* or uintptr_t?
	 */
	pte = mmuwalk(m->pml4, va, 0, 0);
	DBG("physaddr: va %#p pte %#p\n", va, pte);
	if(pte == 0 || (*pte & PteP) == 0)
		return ~(uintmem)0;

	mask = (1ull<<PGSHFT)-1;
	pa = (*pte & ~mask) + (va & mask);

	DBG("physaddr: pte %#p va %#p pa %#llux\n", pte, va, pa);

	return pa;
}

/*
 * Add a device mapping to the vmap range.
 * note that the VMAP and KZERO PDPs are shared
 * between processors (see mpstartap) so no
 * synchronization is being done.
 */
void*
vmap(uintptr_t pa, usize size)
{
	uintptr_t va;
	int o;

	if(pa+size > VMAPSIZE)
		return 0;
	va = pa+VMAP;
	/*
	 * might be asking for less than a page.
	 */
	o = pa & (PGSZ-1);
	pa -= o;
	va -= o;
	size += o;
	pmap(m->pml4, pa | PTEUNCACHED|PTEWRITE|PTEVALID, va, size);
	return (void*)(va+o);
}

void
vunmap(void* v, usize _)
{
	PADDR(v);	/* will panic on error */
}

/*
 * mark pages as write combining (used for framebuffer)
 */
void
patwc(void *a, int n)
{
	uintptr_t *pte, mask, attr, va;
	int z, l;
	long v;

	/* check if pat is usable */
	if((MACHP(0)->cpuiddx & Pat) == 0
	|| rdmsr(0x277, &v) == -1
	|| ((v >> PATWC*8) & 7) != 1)
		return;

	/* set the bits for all pages in range */
	for(va = (uintptr_t)a; n > 0; n -= z, va += z){
		l = 0;
		pte = mmuwalk(m->pml4, va, l, 0);
		if(pte == 0)
			pte = mmuwalk(m->pml4, va, ++l, 0);
		if(pte == 0 || (*pte & PTEVALID) == 0)
			panic("patwc: va=%#p", va);
		z = PGLSZ(l);
		z -= va & (z-1);
		mask = l == 0 ? 3<<3 | 1<<7 : 3<<3 | 1<<12;
		attr = (((PATWC&3)<<3) | ((PATWC&4)<<5) | ((PATWC&4)<<10));
		*pte = (*pte & ~mask) | (attr & mask);
	}
}
