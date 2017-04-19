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
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/umem/internals.h"

/* Pages can have 3 states:
 * - Blank:	never used (pa == 0 && r.ref == 0) just after umem_init
 * - In use:	assigned to at least one slot (pa != 0 && r.ref > 0)
 * - Free:	ready for reuse (pa != 0 && r.ref == 0)
 *
 * We allocate the whole pool on umem_init:
 * - if blankpages reaches zero we refuse to allocate and both
 *   page_new and page_raw will return 0
 * - if freepages reach zero we refuse to allocate and page_new
 *   will return 0 (page_raw does not use free pages)
 * - TODO: evaluate a thresold after which a killer process start
 *   killing unprotected big processes.
 */
typedef struct PagePool
{
	Lock		l;
	Proc*		owner;		/* process currently holding l */
	UserPage*	pages;		/* array of all pages, initialized on umem_init */
	int		npages;		/* number of pages */
	int		freepages;	/* number of free pages */
	int		firstfree;	/* index of first free page */
	int		blankpages;	/* number of blank pages (no physical address assigned yet) */
} PagePool;

static PagePool pool;

static void
plock(void)
{
	lock(&pool.l);
	pool.owner = up;
}
static void
punlock(void)
{
	pool.owner = 0;
	unlock(&pool.l);
}

int
proc_own_pagepool(Proc* p)
{
	return pool.owner == p;
}

/* Initialize the user memory system */
void
umem_init(void)
{
	uintmem avail;
	uint64_t pkb, kkb, kmkb, mkb;

	avail = sys->pmpaged;	/* could include a portion of unassigned memory */
	pool.npages = avail/PGSZ;
	pool.npages -= (pool.npages*sizeof(UserPage)) / PGSZ;	/* little overestimate of the space occupied by the paging structures */
	pool.pages = jehanne_mallocz(pool.npages*sizeof(UserPage), 1);
	if(pool.pages == nil)
		panic("umem_init: out of memory");
	pool.freepages = 0;
	pool.firstfree = pool.npages;
	pool.blankpages = pool.npages;

	/* user, kernel, kernel malloc area, memory */
	pkb = pool.npages*PGSZ/KiB;
	kkb = ROUNDUP((uintptr_t)end - KTZERO, PGSZ)/KiB;
	kmkb = ROUNDDN(sys->vmunmapped - (uintptr_t)end, PGSZ)/KiB;
	mkb = sys->pmoccupied/KiB;

	rawmem_init();
	imagepool_init();

	jehanne_print("%lldM memory: %lldK+%lldM kernel,"
		" %lldM user, %lldM uncommitted\n",
		mkb/KiB, kkb, kmkb/KiB, pkb/KiB, (mkb-kkb-kmkb-pkb)/KiB
	);

}

void
memory_stats(MemoryStats *stats)
{
	uintptr_t km;
	if(stats == nil)
		panic("memory_stats: nil pointer, pc %#p", getcallerpc());
	km = ROUNDUP((uintptr_t)end - KTZERO, PGSZ);
	km += ROUNDDN(sys->vmunmapped - (uintptr_t)end, PGSZ);
	stats->memory = sys->pmoccupied;
	stats->kernel = km;
	stats->user_available = pool.npages*PGSZ;
	stats->user = (pool.npages - pool.blankpages - pool.freepages)*PGSZ;
}

KMap*
kmap2(UserPage* page)	/* TODO: turn this into kmap */
{
	DBG("kmap(%#llux) @ %#p: %#p %#p\n",
		page->pa, getcallerpc(),
		page->pa, KADDR(page->pa));
	return KADDR(page->pa);
}

int
umem_available(uintptr_t need)
{
	return pool.freepages + pool.blankpages > 1 + need/PGSZ;
}

/* Atomically assign a new UserPage to *slot (that must be empty).
 *
 * Returns zero on failure, non zero on success.
 *
 * Failures can be caused by:
 * - not enough memory in the system
 * - slot already filled when the page is ready.
 */
int
page_new(PagePointer *slot, int clear)
{
	UserPage *p;
	KMap *k;
	uintptr_t pa;
	unsigned int pindex;

	MLG("slot %#p, %d clear %d", slot, *slot, clear);
	if(slot == nil)
		panic("page_new: nil slot, pc %#p", getcallerpc());
	if(*slot != 0)
		return 0;	/* save work */

	plock();
	if (pool.firstfree < pool.npages) {
		pindex = pool.firstfree;
		p = &pool.pages[pindex];
		pool.firstfree = p->next;
		adec(&pool.freepages);
	} else if (pool.blankpages > 0) {
		pa = physalloc(PGSZ);
		if(pa == 0)
			panic("page_new: out of memory with %d blanks available, pc %#p", pool.blankpages, getcallerpc());
		pindex = pool.npages - pool.blankpages;
		p = &pool.pages[pindex];
		p->pa = pa;
		adec(&pool.blankpages);
	} else {
		/* no free page and no blank page to allocate */
		punlock();
		return 0;
	}
	punlock();

	p->r.ref = 1;
	p->next = pindex;	/* keep here my own index */

	if(clear) {
		k = kmap2(p);
		jehanne_memset(VA(k), 0, PGSZ);
		kunmap(k);
	}

	if(!cas32(slot, 0, pindex+1)){
		/* the slot is not empty anymore, free the page */
		plock();
		p->r.ref = 0;
		p->next = pool.firstfree;
		pool.firstfree = pindex;
		ainc(&pool.freepages);
		punlock();
		return 0;
	}

	return 1;
}

uintptr_t
page_pa(PagePointer pindex)
{
	UserPage *p;
	if(pindex == 0)
		panic("page_pa: empty PagePointer, pc %#p", getcallerpc());
	if(pindex > pool.npages - pool.blankpages)
		panic("page_pa: out of pool, pc %#p", getcallerpc());
	p = &pool.pages[--pindex];
	MLG("pindex %d pa %#p", pindex+1, p->pa);
	if(p->next != pindex)
		panic("page_pa: free page, pc %#p", getcallerpc());
	return p->pa;
}

char*
page_kmap(PagePointer pindex)
{
	UserPage *p;
	if(pindex == 0)
		panic("page_kmap: empty PagePointer, pc %#p", getcallerpc());
	if(pindex > pool.npages - pool.blankpages)
		panic("page_kmap: out of pool, pc %#p", getcallerpc());
	p = &pool.pages[--pindex];
	MLG("pindex %d pa %#p", pindex+1, p->pa);
	if(p->next != pindex)
		panic("page_kmap: free page, pc %#p", getcallerpc());
	return VA(kmap2(p));
}

void
page_kunmap(PagePointer pindex, char **memory)
{
	UserPage *p;
	if(pindex == 0)
		panic("page_kunmap: empty PagePointer, pc %#p", getcallerpc());
	if(pindex > pool.npages - pool.blankpages)
		panic("page_kunmap: out of pool, pc %#p", getcallerpc());
	p = &pool.pages[--pindex];
	MLG("pindex %d, memory %#p %#p, pa %#p", pindex+1, memory, *memory, p->pa);
	if(p->next != pindex)
		panic("page_kunmap: free page, pc %#p", getcallerpc());
	/* TODO: find a way to panic if *memory&~(PGSZ-1) is not related to p->pa */
	kunmap(*memory);
	*memory = nil;
}

/* Atomically clear *slot and dispose the page previously there.
 *
 * Returns 1 on success, 0 on failure.
 */
int
page_dispose(PagePointer *slot)
{
	UserPage *p;
	unsigned int pindex;

	MLG("slot %#p, %d", slot, *slot);
	if(slot == nil)
		panic("page_dispose: nil slot, pc %#p", getcallerpc());

	pindex = xchg32u(slot, 0);
	if(pindex == 0)
		return 0;
	if(pindex > pool.npages - pool.blankpages)
		panic("page_dispose: out of pool, pc %#p", getcallerpc());
	p = &pool.pages[--pindex];
	if(p->next != pindex)
		panic("page_dispose: already disposed, pc %#p", getcallerpc());
	if(decref(&p->r) == 0){
		plock();
		pindex = p->next;
		p->next = pool.firstfree;
		pool.firstfree = pindex;
		ainc(&pool.freepages);
		punlock();
	}
	return 1;
}


/* Replace the page in *slot with a copy
 *
 * Returns 1 on success, 0 on failure.
 * NOTE: the duplicate of a physical page created by page_rawarea
 * is not physical anymore.
 *
 * Failures can be caused by:
 * - not enough memory in the system
 * - slot empty on entry
 * - slot changed during the operation (should we panic instead?)
 */
int
page_duplicate(PagePointer *slot)
{
	KMap *o, *d;
	unsigned int oindex, dindex = 0;
	UserPage *original, *duplicate;

	MLG("slot %#p, %d", slot, *slot);
	if(slot == nil)
		panic("page_duplicate: nil slot, pc %#p", getcallerpc());
	oindex = *slot;
	if(oindex == 0)
		return 0;
	if(oindex > pool.npages - pool.blankpages)
		panic("page_duplicate: out of pool, pc %#p", getcallerpc());
	original = &pool.pages[oindex-1];
	if(original->next != oindex-1)
		panic("page_duplicate: free page, pc %#p", getcallerpc());

	if(!page_new(&dindex, 0))
		return 0;
	duplicate = &pool.pages[dindex-1];

	o = kmap2(original);
	d = kmap2(duplicate);
	jehanne_memmove(VA(d), VA(o), PGSZ);
	kunmap(d);
	kunmap(o);

	if(cas32(slot, oindex, dindex)){
		page_dispose(&oindex);
		return 1;
	} else {
		page_dispose(&dindex);
		return 0;
	}
}

int
page_duplicate_shared(PagePointer *slot)
{
	PagePointer oindex, nindex;
	UserPage *original;
	MLG("slot %#p, %d", slot, *slot);
	if(slot == nil)
		panic("page_duplicate_shared: nil slot, pc %#p", getcallerpc());
	oindex = *slot;
	if(oindex == 0)
		return 0;
	if(oindex > pool.npages - pool.blankpages)
		panic("page_duplicate_shared: out of pool, pc %#p", getcallerpc());
	original = &pool.pages[oindex-1];

	nindex = oindex;
	if(original->r.ref > 1 && !page_duplicate(&nindex)){
		/* the page was shared, but the duplication failed */
		return 0;
	}
	/* the duplication succeded */
	if(oindex != nindex){
		if(!cas32(slot, oindex, nindex)){
			page_dispose(&nindex);
			return 0;
		}
		return 1;
	}
	/* the page was not shared */
	return 1;
}

int
page_assign(PagePointer *target, PagePointer page)
{
	UserPage *p;
	MLG("target %#p, %d page %d", target, *target, page);
	if(target == nil)
		panic("page_assign: nil target, pc %#p", getcallerpc());
	if(page == 0)
		panic("page_assign: empty page, pc %#p", getcallerpc());
	if(page > pool.npages - pool.blankpages)
		panic("page_assign: out of pool, pc %#p", getcallerpc());
	if(*target != 0)
		return *target == page ? 1 : 0;	/* save work */
	p = &pool.pages[page - 1];
	if(p->next != page-1)
		panic("page_assign: cannot assign a free page, pc %#p", getcallerpc());
	incref(&p->r);
	if(!cas32(target, 0, page)){
		decref(&p->r);
		return *target == page ? 1 : 0;
	}
	return 1;
}

/* Initialize an empty table to map from base to top */
int
table_new(PageTable** table, uintptr_t base, uintptr_t top)
{
	PageTable* new;
	int npages;
	unsigned short mapsize;

	MLG("table %#p, %#p base %#p top %#p", table, *table, base, top);
	if(table == nil)
		panic("table_new: nil table pointer, pc %#p", getcallerpc());
	if(*table != nil){
		panic("table_new: dirty table pointer, pc %#p", getcallerpc());
	}

	base = ROUNDDN(base, PGSZ);
	top = ROUNDUP(top, PGSZ);
	npages = (top - base)>>PGSHFT;
	if(npages > (SEGMAPSIZE*PTEPERTAB))
		return 0;
	mapsize = HOWMANY(npages, PTEPERTAB);
	new = jehanne_mallocz(sizeof(PageTable) + mapsize*sizeof(PageTableEntry*), 1);
	if(new == nil)
		return 0;
	new->base = base;
	new->mapsize = mapsize;
	if(!CASV(table, nil, new))
		panic("table_new: table pointer changed, pc %#p", getcallerpc());

	return 1;
}

/* Lookup the page containing the virtual address va
 *
 * NOTE: it could sleep when an allocation is required, so it must be
 *       called out of any lock at least once.
 */
PagePointer*
table_lookup(PageTable* table, uintptr_t va)
{
	PageTableEntry *pte;
	uintptr_t offset;
	unsigned short pteindex;
	uint8_t pindex, tmp;

	offset = va - table->base;
	pteindex = offset/(PTEPERTAB*PGSZ);
	if(pteindex >= table->mapsize)
		panic("table_lookup: va %#p out of map, pc %#p", va, getcallerpc());
	pte = table->map[pteindex];
	if(pte == nil){
		pte = smalloc(sizeof(PageTableEntry));
		pte->first = PTEPERTAB-1;
		pte->last = 0;
		if(!CASV(&table->map[pteindex], nil, pte)){
			jehanne_free(pte);
			pte = table->map[pteindex];
		}
	}
	pindex = (offset&(PTEMAPMEM-1))>>PGSHFT;

	tmp = pte->first;
	while(tmp > pindex && !cas8(&pte->first, tmp, pindex))
		tmp = pte->first;

	tmp = pte->last;
	while(tmp < pindex && !cas8(&pte->last, tmp, pindex))
		tmp = pte->last;

	MLG("table %#p va %#p pindex %hhud slot %#p current page %ud", table, va, pindex, &pte->pages[pindex], pte->pages[pindex]);
	return &pte->pages[pindex];
}

/* Initialize target table as a copy of the original
 *
 * This code assumes
 * - that nobody can use target while it runs
 * - that nobody can change target pages while it runs
 */
int
table_clone(PageTable* target, PageTable* original)
{
	int entry, page;
	PageTableEntry *pte, *opte;

	if(target == nil)
		panic("table_clone: nil target, pc %#p", getcallerpc());
	if(original == nil)
		panic("table_clone: nil original, pc %#p", getcallerpc());
	if(original->base == 0)
		panic("table_clone: uninitialized original, pc %#p", getcallerpc());
	if(target->base != original->base)
		panic("table_clone: different base, pc %#p", getcallerpc());
	if(target->mapsize != original->mapsize)
		panic("table_clone: different mapsize, pc %#p", getcallerpc());

	MLG("target %#p, original %#p", target, original);
	for(entry = 0; entry < original->mapsize; ++entry){
		opte = original->map[entry];
		if(opte == nil)
			continue;
		pte = jehanne_mallocz(sizeof(PageTableEntry), 1);
		if(pte == nil)
			goto FreePageTableEntries;
		pte->first = opte->first;
		pte->last = opte->last;
		for(page = pte->first; page <= pte->last; ++page){
			if(opte->pages[page] != 0){
				if(!page_assign(&pte->pages[page], opte->pages[page]))
					panic("table_clone: allocated a dirty pte, pc %#p", getcallerpc());
			}
		}
		target->map[entry] = pte;
	}

	return 1;

FreePageTableEntries:
	for(--entry; entry >= 0; --entry){
		pte = target->map[entry];
		if(pte == nil)
			continue;
		for(page = pte->first; page <= pte->last; ++page)
			page_dispose(&pte->pages[page]);
		jehanne_free(pte);
		/* no need to zero new->map[entry]: it dies on jehanne_free(new) */
	}
	return 0;
}

/* dispose the pages in the table, but do not free PageTableEntries */
void
table_free_pages(PageTable* table)
{
	int entry, page;
	PageTableEntry *pte;

	MLG("target %#p", table);
	if(table->base == 0)
		panic("table_free_pages: uninitialized target, pc %#p", getcallerpc());
	for(entry = table->mapsize - 1; entry >= 0; --entry){
		pte = table->map[entry];
		if(pte == nil)
			continue;
		for(page = pte->last; page >= pte->first; --page){
			if(pte->pages[page] == 0)
				continue;
			if(!page_dispose(&pte->pages[page]))
				panic("table_free: concurrent disposition of a page, pc %#p", getcallerpc());
		}
	}
}

void
table_walk_pages(PageTable* table, void (*func)(uintptr_t, UserPage*))
{
	int entry, page;
	PageTableEntry *pte;

	MLG("table %#p", table);
	for(entry = 0; entry < table->mapsize; ++entry){
		pte = table->map[entry];
		if(pte == nil)
			continue;
		for(page = pte->first; page <= pte->last; ++page){
			if(pte->pages[page] == 0)
				continue;
			func(
				table->base+PGSZ*page+PGSZ*PTEPERTAB*entry,
				&pool.pages[pte->pages[page]-1]
			);
		}
	}
}

void
table_free(PageTable** target)
{
	int entry, page;
	PageTable* table;
	PageTableEntry *pte;

	MLG("target %#p %#p", target, *target);
	if(target == nil)
		panic("table_free: nil target, pc %#p", getcallerpc());
	table = xchgm(target, nil);
	if(table == nil)
		panic("table_free: empty target, pc %#p", getcallerpc());
	if(table->base == 0)
		panic("table_free: uninitialized target, pc %#p", getcallerpc());
	for(entry = table->mapsize - 1; entry >= 0; --entry){
		pte = table->map[entry];
		if(pte == nil)
			continue;
		for(page = pte->last; page >= pte->first; --page){
			if(pte->pages[page] == 0)
				continue;
			if(!page_dispose(&pte->pages[page]))
				panic("table_free: concurrent disposition of a page, pc %#p", getcallerpc());
		}
		jehanne_free(pte);	/* this is why we don't simply call table_free_pages */
	}
	jehanne_free(table);
}

int
table_resize(PageTable** target, uintptr_t top)
{
	PageTable *old, *new;
	PageTableEntry *pte;
	int copy, page, npages;

	if(target == nil)
		panic("table_resize: nil target, pc %#p", getcallerpc());
	old = *target;
	if(old == nil)
		panic("table_resize: empty target, pc %#p", getcallerpc());
	if(old->base == 0)
		panic("table_resize: uninitialized target, pc %#p", getcallerpc());

	MLG("target %#p %#p, top %#p", target, *target, top);
	top = ROUNDUP(top, PGSZ);
	npages = (top - old->base)>>PGSHFT;
	if(npages > (SEGMAPSIZE*PTEPERTAB))
		return 0;
	if(old->mapsize >= HOWMANY(npages, PTEPERTAB))
		return 1; /* the table is already big enough */

	new = nil;
	if(!table_new(&new, old->base, top))
		return 0;
	copy = MIN(new->mapsize, old->mapsize);

	jehanne_memmove(new->map, old->map, copy*sizeof(PageTableEntry*));
	if(!CASV(target, old, new))
		panic("table_resize: concurrent resize, pc %#p", getcallerpc());
	while(copy < old->mapsize){
		pte = old->map[copy++];
		if(pte == nil)
			continue;
		for(page = pte->last; page >= pte->first; --page){
			if(pte->pages[page] == 0)
				continue;
			if(!page_dispose(&pte->pages[page]))
				panic("table_resize: concurrent disposition of a page, pc %#p", getcallerpc());
		}
		jehanne_free(pte);
		/* no need to zero old->map[copy-1]: it dies on jehanne_free(old) */
	}
	jehanne_free(old);
	return 1;
}
