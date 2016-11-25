/*
 * Size memory and create the kernel page-tables
 * on the fly while doing so.
 * Called from main(), this code should only be run
 * by the bootstrap processor.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "amd64.h"

#include "acpi.h"

static int npg[4];

#undef DBGFLG
#define DBGFLG 1

RMap	rmapram = {"physical memory"};

static RMap	rmapunavail = {"unavailable physical addresses"};

/*
 * called before first call to asmmapinit/asmmodinit or e820,
 * to mark the kernel text and data out of bounds.
 */
void
asminit(void)
{
	sys->pmstart = ROUNDUP(PADDR(end), PGSZ);
	rmapalloc(&rmapram, 0, sys->pmstart, 0);	/* TO DO: should be empty, surely */
	rmapfree(&rmapunavail, 0, sys->pmstart);
}

static PTE
asmwalkalloc(usize size)
{
	uintmem pa;

	assert(size == PTSZ && sys->vmunused+size <= sys->vmunmapped);

	if((pa = mmuphysaddr(sys->vmunused)) != ~(uintmem)0)
		sys->vmunused += size;

	return pa;
}

void
memmaprange(uintptr_t va, uintmem lo, uintmem hi, PTE (*alloc)(usize), PTE flags)
{
	uintmem mem, nextmem;
	PTE *pte, f;
	int i, l;

	if(alloc == nil)
		alloc = asmwalkalloc;
	/* Convert a range into pages */
	for(mem = lo; mem < hi; mem = nextmem){
		nextmem = (mem + PGLSZ(0)) & ~m->pgszmask[0];

		/* Try large pages first */
		for(i = m->npgsz - 1; i >= 0; i--){
			if((mem & m->pgszmask[i]) != 0)
				continue;
			if(mem + PGLSZ(i) > hi)
				continue;
			/* This page fits entirely within the range. */
			/* Mark it as usable */

			if((l = mmuwalk(va, i, &pte, alloc)) < 0)
				panic("meminit 3");

			f = flags;
			if(l > 0){
				if(f & Pte4KPAT)
					f ^= Pte4KPAT | Pte2MPAT;	/* it's the same for 1G */
				f |= PtePS;
			}
			*pte = mem|PteRW|PteP|f;

			nextmem = mem + PGLSZ(i);
			va += PGLSZ(i);
			npg[i]++;
			break;
		}
	}
}

/*
 * Called after reading the physical memory maps (e820 or multiboot),
 * and following mmuinit, which sets sys->vmstart/vmunmapped/vmunused,
 * the boundaries of the initial, contiguous kernel virtual address space.
 *
 * Extend the page tables to complete the mapping of physical memory
 * into the region beyond vmunmapped, claiming the memory from the
 * physical memory map; and map the remaining chunks of
 * physical memory into the region KSEG2.
 */
void
meminit(void)
{
	RMapel *adrsm;
	uintptr_t va;
	uintmem hi, lo, n;

	/*
	 * do we need a map, like vmap, for best use of mapping kmem?
	 * - in fact, a rewritten pdmap could do the job, no?
	 * have to assume up to vmend is contiguous.
	 * can't mmuphysaddr(sys->vmunmapped) because...
	 */

	/* assume already 2MiB aligned and 2MiB pages available */
	assert(m->pgszlg2[1] == 21);
	assert((sys->vmunmapped & m->pgszmask[1]) == 0);

	print("vmunmapped %#llux\n", sys->vmunmapped);

	n = TMFM;
	if(n > sys->pmoccupied)
		n = sys->pmoccupied/2;
	sys->pmunassigned = ROUNDDN(n, MiB);

	for(adrsm = rmapram.map; adrsm != nil; adrsm = adrsm->next){
		va = KSEG2+adrsm->addr;
		lo = adrsm->addr;
		hi = adrsm->addr+adrsm->size;
		DBG("mem %#P %#P (%P) va %#p\n", lo, hi, hi-lo, va);
		memmaprange(va, lo, hi, asmwalkalloc, 0);
	}

	n = sys->pmoccupied;
	if(n > 600*MiB)
		n = 600*MiB;
	ialloclimit(n/3);
}

void
memdebug(void)
{
	int i;
	if(DBGFLG || 1){
		rmapprint(&rmapram);
		rmapprint(&rmapunavail);
		print("k ptes:");
		for(i = 0; i < nelem(npg); i++)
			print(" %d", npg[i]);
		print("\n");
	}
}

void
memreserve(uintmem pa, uintmem size)
{
	rmapfree(&rmapunavail, pa, size);
}

void
memaffinity(uint64_t base, uint64_t len, uint32_t dom, int flags)
{
	if(flags & MemNonVolatile){
		memreserve(base, len);
		return;
	}
	DBG("mem affinity: %#16.16llux %#16.16llux -> %d\n", base, base+len-1, dom);
	/* TO DO: add [base, base+len[ to dom table */
}

void*
basealloc(usize nb, uint32_t align, usize *alloced)
{
	uintmem pa;

	if(align < CACHELINESZ)
		align = CACHELINESZ;
	nb = ROUNDUP(nb, CACHELINESZ);
	pa = rmapalloc(&rmapram, 0, nb, align);
	if(pa == 0)
		return nil;
	if(alloced != nil)
		*alloced = nb;
	return KADDR(pa);
}

void
basefree(void *p, usize nb)
{
	uintmem pa;

	pa = PADDR(p);
	rmapfree(&rmapram, pa, nb);
}

int
e820(void)
{
	char *p, *s;
	uint64_t base, len;
	uint32_t type;
	int v;

	p = getconf("*e820");
	if(p == nil)
		return 0;
	v = 0;
	for(s = p;;){
		if(*s == 0)
			break;
		type = strtoul(s, &s, 16);
		if(*s != ' ')
			break;
		base = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		len = strtoull(s, &s, 16) - base;
		if(*s != ' ' && *s != 0 || len == 0)
			break;
		DBG("E820: %llux %llux %#ux\n", base, len, type);
		asmmapinit(base, len, type);
		v = 1;
	}
	return v;
}

/*
 * Notes:
 * asmmapinit and asmmodinit called from multiboot or e820;
 * subject to change; the numerology here is probably suspect.
 * Multiboot defines the alignment of modules as 4096.
 */
void
asmmapinit(uintmem addr, uintmem size, int type)
{
	if(type == AddrsMemory){
		/*
		 * Adjust things for the peculiarities of this
		 * architecture.
		 */
		if(addr < 1*MiB || addr+size < sys->pmstart)
			return;
		if(addr < sys->pmstart){
			size -= sys->pmstart - addr;
			addr = sys->pmstart;
		}
		rmapfree(&rmapram, addr, size);
		sys->pmoccupied += size;
	}else if(specialmem != nil)
		specialmem(addr, size, type);
	rmapfree(&rmapunavail, addr, size);
}

void
asmmodinit(uint32_t start, uint32_t end, char* _1)
{
	if(start < sys->pmstart)
		return;
	end = ROUNDUP(end, 4096);
	if(end > sys->pmstart){
		rmapalloc(&rmapram, sys->pmstart, end-sys->pmstart, 0);
		sys->pmstart = end;
	}
}
