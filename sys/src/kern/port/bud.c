#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * - locks
 * - could instead coalesce free items on demand (cf. Wulf)
 * - or lazy buddy (cf. Barkley)
 */

enum{
	MinK=		PGSHFT,		/* default minimum size (one page) */
	Nbits=		sizeof(uintmem)*8,
	MaxK=		Nbits-1,		/* last usable k (largest block is 2^k) */

	Busy=		0x80,	/* bit set in byte map if block busy (low order bits are block size, 0=unavailable) */
};

//#define	usize	uintmem

typedef struct Blk Blk;
struct Blk{
	Blk*	forw;	/* free list */
	Blk*	back;
};

typedef struct Bfree Bfree;
struct Bfree{
	Blk		h;	/* header */
	Lock;
	uint32_t	avail;
};

struct Bpool{
	Lock	lk;	/* TO DO: localise lock (need CAS update of pool->kofb) (also see Johnson & Davis 1992) */
	Bfree	blist[Nbits];	/* increasing powers of two */
	uint8_t*	kofb;		/* k(block_index) with top bit set if busy */
	uint32_t	mink;
	uint32_t	maxk;
	uint32_t	maxb;	/* limit to block index, in minbsize blocks (pool size) */
	Blk*	blocks;	/* free list pointers */
	uintmem	base;
	uintmem	minbsize;
	uintmem	limit;
};

static uint8_t lg2table[256] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

#define	BI(a)	((a)>>pool->mink)
#define	IB(x)	((uintmem)(x)<<pool->mink)

static int
lg2ceil(uintmem m)
{
	uint32_t n, h;
	int r;

	r = (m & (m-1)) != 0;	/* not a power of two => round up */
	n = (uint32_t)m;
	if(sizeof(uintmem)>sizeof(uint32_t)){
		h = (uint64_t)m>>32;
		if(h != 0){
			n = h;
			r += 32;
		}
	}
	if((n>>8) == 0)
		return lg2table[n] + r;
	if((n>>16) == 0)
		return 8 + lg2table[n>>8] + r;
	if((n>>24) == 0)
		return 16 + lg2table[n>>16] + r;
	return 24 + lg2table[n>>24] + r;
}

Bpool*
bpoolcreate(uint32_t mink, uint32_t maxk, uintmem base, uintmem top, void* (*alloc)(usize, int))
{
	int k;
	Blk *b;
	Bpool *pool;

	if(mink == 0)
		mink = MinK;
	if(maxk > MaxK)
		panic("bpoolcreate");
	pool = alloc(sizeof(Bpool), 1);
	if(pool == nil)
		panic("bpoolcreate alloc");
	pool->mink = mink;
	pool->maxk = maxk;
	pool->base = base;
	pool->limit = top;
	pool->maxb = BI(top-base);
	pool->minbsize = (uintmem)1<<mink;
	pool->blocks = alloc((pool->maxb+1)*sizeof(*pool->blocks), 0);
	if(pool->blocks == nil)
		panic("bpoolinit: can't allocate %ud blocks", pool->maxb+1);
	for(k = 0; k < nelem(pool->blist); k++){
		b = &pool->blist[k].h;
		b->forw = b->back = b;
	}
	pool->kofb = alloc((pool->maxb+1)*sizeof(*pool->kofb), 1);
	if(pool->kofb == nil)
		panic("physinit: can't allocate %ud kofb", pool->maxb+1);
	print("pool %#p space base %#P top=%#P maxb=%#ux (%d)\n", pool, base, top, pool->maxb, pool->maxb);
	return pool;
}

uintmem
bpoolalloc(Bpool *pool, usize size)
{
	int j, k;
	Blk *b, *b2;
	uintmem a, a2;
	uint32_t bi;

	k = lg2ceil(size);
	if(k < pool->mink)
		k = pool->mink;
	if(k > pool->maxk)
		return 0;
	DBG("%#p size=%#P k=%d\n", pool, (uintmem)size, k);
	lock(&pool->lk);
	for(j = k;;){
		b = pool->blist[j].h.forw;
		if(b != &pool->blist[j].h)
			break;
		if(++j > pool->maxk){
			unlock(&pool->lk);
			return 0;	/* out of space */
		}
	}
	if(b == nil)
		panic("physalloc: nil");
	/* set busy state */
	bi = b - pool->blocks;
	if(pool->kofb[bi] & Busy || b->forw == nil || b->back == nil)
		panic("physalloc: inval k=%d j=%d %#p %d %#ux %#p %#p", k, j, b, bi, pool->kofb[bi], b->forw, b->back);
	pool->kofb[bi] = k | Busy;
	pool->blist[j].avail--;
	b->forw->back = b->back;
	b->back->forw = b->forw;
	a = IB(bi);
	while(j != k){
		/* split */
		j--;
		a2 = a+((uintmem)1<<j);
		bi = BI(a2);
		DBG("split %#llux %#llux k=%d %#llux pool->kofb=%#ux\n", a, a2, j, (uintmem)1<<j, pool->kofb[bi]);
		if(pool->kofb[bi] & Busy){
			if(pool->kofb[bi] & ~Busy)
				panic("bal: busy block %#llux k=%d\n", a, pool->kofb[bi] & ~Busy);
		}
		pool->kofb[bi] = j;	/* new size, not busy */
		b2 = &pool->blocks[bi];
		b2->forw = &pool->blist[j].h;
		b2->back = pool->blist[j].h.back;
		pool->blist[j].h.back = b2;
		b2->back->forw = b2;
		pool->blist[j].avail++;
	}
	unlock(&pool->lk);
	return a + pool->base;
}

void
bpoolfree(Bpool *pool, uintmem a, usize size)
{
	int k;
	Blk *b, *b2;
	uintmem a2;
	uint32_t bi, bi2;

	k = lg2ceil(size);	/* could look it up in pool->kofb */
	if(k < pool->mink)
		return;
	if(k > pool->maxk)
		k = pool->maxk;
	DBG("%#p free %#llux %#P k%d\n", pool, a, (uintmem)size, k);
	if(a < pool->base)
		panic("bpoolfree");
	a -= pool->base;
	bi = BI(a);
	lock(&pool->lk);
	if(pool->kofb[bi] != 0 && pool->kofb[bi] != (Busy|k)){
		unlock(&pool->lk);
		panic("balfree: busy %#llux odd k k=%d kofb=%#ux\n", a, k, pool->kofb[bi]);
	}
	for(; k != pool->maxk; k++){
		pool->kofb[bi] = Busy;
		a2 = a ^ ((uintmem)1<<k);	/* buddy */
		bi2 = BI(a2);
		b2 = &pool->blocks[bi2];
		if(bi2 >= pool->maxb || pool->kofb[bi2] != k)
			break;
		/* valid, not busy or empty, size k */
		DBG("combine %#llux %#llux %d %#llux\n", a, a2, k, (uintmem)1<<k);
		b2->back->forw = b2->forw;
		b2->forw->back = b2->back;
		pool->kofb[bi2] = Busy;
		pool->blist[k].avail--;
		if(a2 < a){
			a = a2;
			bi = bi2;
		}
	}
	pool->kofb[bi] = k;	/* sets size and resets Busy */
	b = &pool->blocks[bi];
	b->forw = &pool->blist[k].h;
	b->back = pool->blist[k].h.back;
	pool->blist[k].h.back = b;
	b->back->forw = b;
	pool->blist[k].avail++;
	unlock(&pool->lk);
}

void
bpoolallocrange(Bpool *pool, usize *low, usize *high)
{
	*low = (usize)1<<pool->mink;
	*high = (usize)1<<pool->maxk;
}

static void
ibpoolfree(Bpool *pool, uintmem base, usize size)
{
	bpoolfree(pool, base+pool->base, size);
}

void
bpoolinitfree(Bpool *pool, uintmem base, uintmem lim)
{
	uintmem m, size;
	int i;

	/* chop limit to min block alignment */
	if(base >= pool->limit)
		return;
	if(pool->base > base)
		base = pool->base;
	if(lim > pool->limit)
		lim = pool->limit;
	base -= pool->base;
	lim -= pool->base;
	lim &= ~(pool->minbsize-1);
	if(BI(lim) > pool->maxb){
		print("physinitfree: address space too large");
		lim = IB(pool->maxb);
	}

	/* round base to min block alignment */
	base = (base + pool->minbsize-1) & ~(pool->minbsize-1);

	size = lim - base;
	if(size < pool->minbsize)
		return;
	DBG("bpoolinitfree %#p %#P-%#P [%#P]\n", pool, pool->base+base, pool->base+lim, size);

	/* move up from base in largest blocks that remain aligned */
	for(i=pool->mink; i<pool->maxk; i++){
		m = (uintmem)1 << i;
		if(base & m){
			if(size < m)
				break;
			if(base & (m-1)){
				print(" ** error: %#P %#P\n", base, m);
				return;
			}
			ibpoolfree(pool, base, m);
			base += m;
			size -= m;
		}
	}

	/* largest chunks, aligned */
	m = (uintmem)1<<pool->maxk;
	while(size >= m){
		if(base & (m-1)){
			print(" ** error: %#P %#P\n", base, m);
			return;
		}
		ibpoolfree(pool, base, m);
		base += m;
		size -= m;
	}

	/* free remaining chunks, decreasing alignment */
	for(; size >= pool->minbsize; m >>= 1){
		if(size & m){
			DBG("\t%#P %#P\n", base, m);
			if(base & (m-1)){
				print(" ** error: %#P %#P\n", base, m);
				return;
			}
			ibpoolfree(pool, base, m);
			base += m;
			size &= ~m;
		}
	}
}

char*
seprintbpoolstats(Bpool *pool, char *s,  char *e)
{
	Bfree *b;
	int i;

	lock(&pool->lk);
	for(i = 0; i < nelem(pool->blist); i++){
		b = &pool->blist[i];
		if(b->avail != 0)
			s = seprint(s, e, "%ud %ulldK blocks avail\n",
				b->avail, (1ull<<i)/KiB);
	}
	unlock(&pool->lk);
	return s;
}

void
bpooldump(Bpool *pool)
{
	uintmem a;
	uint32_t bi;
	int i, k;
	Blk *b;

	for(i=0; i<nelem(pool->blist); i++){
		b = pool->blist[i].h.forw;
		if(b != &pool->blist[i].h){
			print("%d	", i);
			for(; b != &pool->blist[i].h; b = b->forw){
				bi = b-pool->blocks;
				a = IB(bi);
				k = pool->kofb[bi];
				print(" [%#llux %d %#ux b=%#llux]", a, k, 1<<k, a^((uintmem)1<<k));
			}
			print("\n");
		}
	}
}
