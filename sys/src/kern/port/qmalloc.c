/*
 * malloc
 *
 *	C B Weinstock and W A Wulf, "Quick Fit: An Efficient Storage Algorithm for Heap Storage Allocation",
 *		SIGPLAN Notices, 23(10), 144-148 (1988).
 *	A Iyengar, "Scalability of Dynamic Storage Allocation Algorithms", IEEE Proceedings Frontiers of
 *		Massively Parallel Computing, 223-232 (1996).
 *	Yi Feng and Emery D Berger, "A Locality-Improving Dynamic Memory Allocator",
 *		ACM Proceedings of the 2005 workshop on Memory system performance, 68-77 (2005).
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

typedef union Header Header;
typedef struct Qlist Qlist;
typedef struct Region Region;
typedef struct BigAlloc BigAlloc;

enum{
	Bpw=	8*sizeof(uint32_t),
	Busy= 	(uint32_t)1<<(Bpw-1),		/* set in Header.s.size of allocated blocks */
	Poisoning=	1,	/* or DBGFLG */	/* DEBUG: poison block */
};

/*
 * Memory is allocated in units of Header,
 * which also provides the basic alignment.
 * Every usable block is at least 2 units: the header
 * and at least one unit of data. The block heading
 * the misc list has size 0 and will never be allocated.
 *
 * Yi Feng and Berger eliminate the object headers for
 * quick fit items, since they are never coalesced,
 * but we retain headers to track ownership.
 */
union Header
{
	struct {
		uint32_t	size;		/* size in units including Header, and Busy flag */
		uint32_t	tag;		/* only for debugging: set malloctag */
		Header*	next;		/* free list */
	} s;
	uint8_t	_align[8];
};

enum{
	Unitsz=	sizeof(Header),		/* must be sizeof(Header) */
	MinUnits=	2,
	Align=	Unitsz,		/* minimum alignment */
};

/* application pointers to and from headers */
#define	D2H(p)	((Header*)(p)-1)
#define	H2D(h)	((void*)((h)+1))

#define	XX(P,N)	iprint("[%d]: %#p -> %#p\n", (N), (P), (P)->s.next)
#undef XX
#define	XX(P,N)

struct Qlist
{
	Lock	lk;
	Header*	first;

	uint32_t	nalloc;
};

struct Region
{
	Lock	lk;
	Region*	down;	/* run as a stack */
	Header*	start;
	Header*	tail;	/* next available, if any */
	Header*	end;
	uint32_t	size;		/* remaining space, in Unitsz */
	char*	base;
	char*	limit;
};

/*
 * big mallocs out of basealloc
 */
enum
{
	BHashSize=	67,	/* prime */
	NBig=	512,
	BigThreshold=	8*1024*1024,
};

struct BigAlloc
{
	uintptr_t	va;
	uintptr_t	nbytes;
	uintptr_t	tag;
	BigAlloc*	next;
};

static struct {
	Lock;
	BigAlloc*	free;
	BigAlloc	pool[NBig];	/* could qalloc them */

	BigAlloc*	set[BHashSize];
} bighash;

#define	BIGHASH(p)		((uintptr_t)p%BHashSize)
#define	isbigalloc(p)	((uintptr_t)(p)>=KSEG2 && (uintptr_t)(p)<KSEG1)

/* TO DO: fewer buckets for larger blocks */
#define	NUNITS(n)	(HOWMANY(n, Unitsz) + 1)
#define	NQUICK		((4096/Unitsz)+1)

static	Qlist	quicklist[NQUICK+1];
static	Header	misclist;
static	Header	*rover;	/* through misc list over all Regions */
static	Region	*tail;		/* current tail Region */
static	int	morecore(unsigned);
//static	void	coalesce(Region*);	// not used
static	void	freemisc(Header*);
static	BigAlloc*	bigfind(void*);
static	void	bignote(void*, usize, uintptr_t);
static	void*	bigalloc(usize, usize, uintptr_t);
static	void	bigfree(void*);

static void	(*memprof)(void*, uint32_t, usize, int) = nil;

enum
{
	QSmalign = 0,
	QSmalignquick,
	QSmalignrover,
	QSmalignfront,
	QSmalignback,
	QSmaligntail,
	QSmalignnottail,
	QSmalloc,
	QSmallocrover,
	QSmalloctail,
	QSfree,
	QSfreetail,
	QSfreequick,
	QSfreenext,
	QSfreeprev,
	QSsplitquick,
	QSsplitmisc,
	QSmax
};

static	char*	qstatname[QSmax] = {
[QSmalign]		"malign",
[QSmalignquick]	"malignquick",
[QSmalignrover]	"malignrover",
[QSmalignfront]	"malignfront",
[QSmalignback]	"malignback",
[QSmaligntail]	"maligntail",
[QSmalignnottail]	"malignnottail",
[QSmalloc]		"malloc",
[QSmallocrover]	"mallocrover",
[QSmalloctail]	"malloctail",
[QSfree]		"free",
[QSfreetail]	"freetail",
[QSfreequick]	"freequick",
[QSfreenext]	"freenext",
[QSfreeprev]	"freeprev",
[QSsplitquick]	"split quick",
[QSsplitmisc]	"split misc",
};

static	void*	qalloc(usize, uintptr_t);
//static	void	qfreeinternal(Header*); // not defined
static	int	qstats[QSmax];

static	void*	bigalloc(usize, usize, uintptr_t);
static	void	bigfree(void*);

static	Lock		mainlock;

/* misc lock (TO DO: per misclist, and Region/Reap) */
#define	MLOCK		ilock(&mainlock)
#define	MUNLOCK	iunlock(&mainlock)

#define ISPOWEROF2(x)	(/*((x) != 0) && */!((x) & ((x)-1)))
#define ALIGNHDR(h, a)	(Header*)((((uintptr_t)(h))+((a)-1)) & ~((a)-1))

static Header*
tailalloc(Region *r, int n)
{
	Header *p;

	qstats[QSmalloctail]++;
	p = r->tail;
	if(n == 0)
		panic("tailalloc size");
	if(r->size < n)
		panic("tailalloc");
	r->size -= n;
	r->tail += n;
	p->s.size = Busy | n;
	return p;
}

static int
freetail(Header *h)
{
	if(h+h->s.size == tail->tail) {	/* worthwhile locking? */
		MLOCK;
		if(h+h->s.size == tail->tail){
			/* block before tail */
			tail->tail = h;
			tail->size += h->s.size;
			qstats[QSfreetail]++;
			MUNLOCK;
			return 1;
		}
		MUNLOCK;
	}
	return 0;
}

static void
freequick(Header *t)
{
	Qlist *ql;

	ql = &quicklist[t->s.size];
	ilock(&ql->lk);
	t->s.next = ql->first;
	ql->first = t;
	iunlock(&ql->lk);
	qstats[QSfreequick]++;
}

int
malloclocked(void)
{
	if(canlock(&mainlock)){
		unlock(&mainlock);
		return 0;
	}
	return 1;
}

static void
showchain(Header *p)
{
	Header *q;

	iprint("chain %#p:\n", p);
	q = p;
	do{
		iprint("%#p [%#ux %#ux] -> %#p\n", q, q->s.size, q->s.tag, q->s.next);
	}while((q = q->s.next) != nil && q->s.next != p);
}

static void
badchain(char *tag, char *why, Header *p, Header *q, Header *r, void *h, Header *split)
{
	showchain(&misclist);
	if(split != nil)
		panic("%s: %s: %#p %#ux %#ux -> %#p %#ux %#ux [%#p -| %#p]%s %#p %#ux %#ux\n", tag, why,
			q, q->s.size, q->s.tag,
			r, r->s.size, r->s.tag,
			p, h, " split", split, split->s.size, split->s.tag);
	else
		panic("%s: %s: %#p %#ux %#ux -> %#p %#ux %#ux [%#p -| %#p]\n", tag, why,
			q, q->s.size, q->s.tag,
			r, r->s.size, r->s.tag,
			p, h);
}

static Header*
contained(Header *b, Header *chain)
{
	Header *q;
	int i;

	i = 0;
	q = chain;
	do{
		if(q->s.next == nil)
			panic("nil contained");
		if(q->s.next == b)
			return q;
	}while((q = q->s.next) != chain && ++i < 100000);
	if(i >= 100000)
		print("long check chain\n");
	return 0;
}

static void
checkchain(Header *p, char *tag, void *h, Header *split)
{
	Header *q, *r;
	int i;

	q = p;
	i = 0;
	do{
		if(q->s.next == nil)
			badchain(tag, "nil next", p, q, q, h, split);
		if((r = q->s.next)->s.size & Busy)
			badchain(tag, "busy", p, q, r, h, split);
		if(split && r == h)
			badchain(tag, "mentioned", p, q, r, h, split);
	}while((q = q->s.next) != p && ++i < 100000);
	if(i >= 100000)
		print("long chain\n");
}

static void*
qallocalign(usize nbytes, uintptr_t align, long offset, usize span, uintptr_t pc)
{
	Qlist *qlist;
	uintptr_t aligned;
	Header **pp, *p, *q, *r;
	uint32_t n, nunits, alunits, maxunits, gap;

	if(nbytes == 0 || offset != 0 || span != 0)
		return nil;

	if(!ISPOWEROF2(align))
		panic("qallocalign");

	if(align <= Align)
		return qalloc(nbytes, pc);

	qstats[QSmalign]++;
	nunits = NUNITS(nbytes);
	if(nunits <= NQUICK){
		/*
		 * Look for a conveniently aligned block
		 * on one of the quicklists.
		 */
		qlist = &quicklist[nunits];
		ilock(&qlist->lk);
		for(pp = &qlist->first; (p = *pp) != nil; pp = &p->s.next){
			if(ALIGNED(p+1, align)){
				*pp = p->s.next;
				p->s.size |= Busy;
				qstats[QSmalignquick]++;
				iunlock(&qlist->lk);
				p->s.tag = pc;
				return H2D(p);
			}
		}
		iunlock(&qlist->lk);
	}

	alunits = HOWMANY(align, Unitsz);
	maxunits = nunits + alunits + MinUnits;
	MLOCK;
	if(maxunits > tail->size) {
		/* hard way */
		q = rover;
		do {
			p = q->s.next;
			aligned = ALIGNED(p+1, align);
			if(aligned && p->s.size >= nunits || p->s.size >= maxunits){

				/*
				 * This block is big enough
				 */
				qstats[QSmalignrover]++;

				/*
				 * Leave behind any runt in front of the alignment;
				 * it will be coalesced if the aligned memory is released.
				 */
				if(!aligned){
					r = p;
					p = ALIGNHDR(p+1, align) - 1;
					if(!ALIGNED(p+1, align))
						panic("qmallocalign");
					n = p - r;
					p->s.size = r->s.size - n;
					if(r->s.size <= 1 || n <= 1)
						panic("qallocalign size");
					p->s.size = n;
					p->s.next = r->s.next;
					q = r;
					qstats[QSmalignfront]++;
				}

				q->s.next = p->s.next;
				XX(q,1);
				rover = q;

				/*
				 * Leave behind any residue after the aligned block.
				 */
				if(p->s.size > nunits+MinUnits){
					r = p + nunits;
					r->s.size = p->s.size - nunits;
					r->s.next = q->s.next;
					q->s.next = r;
					XX(q,2);
					qstats[QSmalignback]++;
					p->s.size = nunits;
				}

				p->s.size |= Busy;
				MUNLOCK;

				p->s.next = nil;
				p->s.tag = pc;
				return H2D(p);
			}
		} while((q = p) != rover);

		/* grow tail */
		if(!morecore(maxunits+MinUnits)){
			MUNLOCK;
			return nil;
		}
	}

	q = tail->tail + 1;
	p = ALIGNHDR(q, align);
	gap = p - q;
	if(gap == 0){
		p = tailalloc(tail, nunits);
		if(!ALIGNED(p+1, align))
			panic("qmallocalign 2");
		qstats[QSmaligntail]++;
	}else{
		if(gap < MinUnits)
			gap += alunits;
		if(tail->size < nunits+gap)
			panic("qallocalign: miscalculation tail=%ud maxunits=%ud nunits=%ud gap=%ud", tail->size, maxunits, nunits, gap);
		/*
		 * Save the residue before the aligned allocation
		 * and free it after the tail pointer has been bumped
		 * for the main allocation.
		 */
		r = tailalloc(tail, gap);
		p = tailalloc(tail, nunits);
		if(!ALIGNED(p+1, align))
			panic("qmallocalign 3");
		qstats[QSmalignnottail]++;
		freemisc(r);	/* put on misc list to allow combining if this block is freed */
	}
	MUNLOCK;

	p->s.next = nil;
	p->s.tag = pc;
	return H2D(p);
}

static void*
qalloc(usize nbytes, uintptr_t pc)
{
	Qlist *qlist;
	Header *p, *q, *t;
	uint32_t nunits, u;
	int split;

	if(nbytes == 0)
		return nil;

	nunits = NUNITS(nbytes);
	for(u = nunits; u <= NQUICK; u++){
		qlist = &quicklist[u];
		ilock(&qlist->lk);
		if((p = qlist->first) != nil){
			qlist->first = p->s.next;
			qlist->nalloc++;
			iunlock(&qlist->lk);
			p->s.next = nil;
			if(p->s.size >= nunits+2*MinUnits){	/* don't make blocks pointlessly small */
				t = p;
				t->s.size -= nunits;
				p += t->s.size;
				p->s.size = nunits;
				freequick(t);
				qstats[QSsplitquick]++;
			}
			p->s.size |= Busy;
			p->s.tag = pc;
			return H2D(p);
		}
		iunlock(&qlist->lk);
	}

	MLOCK;
	if(nunits > tail->size) {
		/* hard way */
		q = rover;
		split = 0;
		checkchain(q, "qalloc-1", rover, 0);
		do {
			p = q->s.next;
			if(p->s.size & Busy)
				panic("qalloc: busy: %#p %#ux ~ %#ux\n", p, p->s.size, p->s.tag);
			if(p->s.next->s.size & Busy)
				panic("qalloc+: busy");
			if(p->s.next->s.next->s.size & Busy)
				panic("qalloc++: busy");
			if(p->s.size >= nunits) {
				if(p->s.size > nunits+MinUnits) {	/* split block; return tail */
					p->s.size -= nunits;
					p += p->s.size;
					p->s.size = nunits;
					split = 1;
					if(contained(p, rover))
						badchain("qalloc-3", "contained", rover, q, q->s.next, p, q->s.next);
					qstats[QSsplitmisc]++;
				}else{
					q->s.next = p->s.next;
					XX(q,3);
				}
				rover = q;
				qstats[QSmallocrover]++;
				if(contained(p, rover))
					badchain("qalloc-4", "contained", rover, q, p, p, nil);
				checkchain(q, "qalloc-2", p, split? q->s.next: nil);
				p->s.size |= Busy;
				MUNLOCK;
				p->s.tag = pc;
				return H2D(p);
			}
		} while((q = p) != rover);

		/* grow tail */
		if(!morecore(nunits)){
			MUNLOCK;
			return nil;
		}
	}
	p = tailalloc(tail, nunits);
	MUNLOCK;
	p->s.tag = pc;
	return H2D(p);
}

static void
freemisc(Header *p)
{
	Header *q, *x;

	p->s.size &= ~Busy;
	q = rover;
	checkchain(q, "qfree-1", p, 0);
	for(; !(p > q && p < q->s.next); q = q->s.next)
		if(q >= q->s.next && (p > q || p < q->s.next)){	/* put freed block at start or end of arena */
			iprint("q==%#p %#ux %#p p==%#p %#ux\n", q, q->s.size, q->s.next, p, p->s.size);
			break;
		}
	if((x = contained(p, &misclist)) != nil)
		badchain("qfree-1a", "contained", x, q, p, p, nil);
	if(p+p->s.size == q->s.next) {
		p->s.size += q->s.next->s.size;
		p->s.next = q->s.next->s.next;
		XX(p,4);
		qstats[QSfreenext]++;
	}else{
		p->s.next = q->s.next;
		XX(p,5);
	}
	if(q+q->s.size == p) {
		q->s.size += p->s.size;
		q->s.next = p->s.next;
		XX(q,6);
		qstats[QSfreeprev]++;
		if(contained(p, &misclist))
			badchain("qfree-1b", "contained", rover, q, p, p, nil);
	}else{
		q->s.next = p;
		XX(q,7);
	}
	checkchain(q, "qfree-2", p, 0);
	rover = q;
}

uint32_t
msize(void* ap)
{
	BigAlloc *b;
	Header *h;
	uint32_t nunits;

	if(ap == nil)
		return 0;

	if(isbigalloc(ap)){
		b = bigfind(ap);
		if(b != nil)
			return b->nbytes;
	}
	h = D2H(ap);
	nunits = h->s.size & ~Busy;
	if((h->s.size & Busy) == 0 || nunits == 0)
		panic("msize: corrupt allocation arena");

	return (nunits - 1) * Unitsz;
}

static void
mallocreadfmt(char* s, char* e)
{
	char *p;
	Header *q;
	int i, n;
	Qlist *qlist;
	Region *r;
	uintmem t, u;

	p = s;
	MLOCK;
	t = 0;
	n = 0;
	u = 0;
	for(r = tail; r != nil; r = r->down){
		p = seprint(p, e, "reg%d: %#p %#p %#p : %#p %#p\n", n, r, r->base, r->limit, r->start, r->tail);
		t += r->limit - r->base;
		u += (r->tail - r->start)*Unitsz;
		n++;
	}
	MUNLOCK;
	p = seprint(p, e, "%P kernel malloc %P used %d regions\n", t, u, n);
	p = seprint(p, e, "0/0 kernel draw\n"); // keep scripts happy

	t = 0;
	for(i = 0; i <= NQUICK; i++) {
		n = 0;
		qlist = &quicklist[i];
		ilock(&qlist->lk);
		for(q = qlist->first; q != nil; q = q->s.next)
			n++;
		iunlock(&qlist->lk);

		if(n != 0)
			p = seprint(p, e, "q%d %ud %ud %ud\n", i, n, n*i*Unitsz, qlist->nalloc);
		t += n * i*Unitsz;
	}
	p = seprint(p, e, "quick: %P bytes total\n", t);

	MLOCK;
	if((q = rover) != nil){
		i = t = 0;
		do {
			t += q->s.size;
			i++;
//			p = seprint(p, e, "m%d\t%#p\n", q->s.size, q);
		} while((q = q->s.next) != rover);

		p = seprint(p, e, "rover: %d blocks %P bytes total\n",
			i, t*Unitsz);
	}
	MUNLOCK;

	for(i = 0; i < nelem(qstats); i++)
		if(qstats[i] != 0)
			p = seprint(p, e, "%s: %ud\n", qstatname[i], qstats[i]);
	USED(p);
}

long
mallocreadsummary(Chan* _1, void *a, long n, long offset)
{
	char *alloc;

	alloc = malloc(READSTR);
	if(waserror()){
		free(alloc);
		nexterror();
	}
	mallocreadfmt(alloc, alloc+READSTR);
	n = readstr(offset, a, n, alloc);
	poperror();
	free(alloc);

	return n;
}

#if 0
static void
coalesce(Region *r)
{
	Header *p, *q;

	ilock(&r->lk);
	/* TO DO: need to re-establish the local free list */
	for(p = r->start; p != r->tail;){
		if((p->s.size & Busy) == 0){
			if((q = p->s.next) != nil && (q->s.size & Busy) == 0)
				p->s.size += q->s.size;
		}else
			p += p->s.size & ~Busy;
	}
	iunlock(&r->lk);
}
#endif

/*
 * big allocations use basealloc
 */
static BigAlloc*
bigfind(void *p)
{
	BigAlloc *b, **l;

	/* quick check without lock is fine: p can't be added meanwhile */
	l = &bighash.set[BIGHASH(p)];
	if(*l == nil)
		return nil;
	ilock(&bighash);
	for(; (b = *l) != nil; l = &b->next)
		if(b->va == (uintptr_t)p)
			break;
	iunlock(&bighash);
	return b;
}

static void
bignote(void *p, usize nbytes, uintptr_t pc)
{
	BigAlloc *b, **l;

	ilock(&bighash);
	b = bighash.free;
	if(b == nil)
		panic("bigstore: no free structures");
	bighash.free = b->next;
	b->va = (uintptr_t)p;
	b->nbytes = nbytes;
	b->tag = pc;
	l = &bighash.set[BIGHASH(p)];
	b->next = *l;
	*l = b;
	iunlock(&bighash);
}

static void*
bigalloc(usize nbytes, usize align, uintptr_t pc)
{
	void *p;

	p = basealloc(nbytes, align, &nbytes);
	if(p == nil)
		return p;
	bignote(p, nbytes, pc);
	return p;
}

static void
bigfree(void *p)
{
	BigAlloc **l, *b;
	uintmem used;

	if(p == nil)
		return;
	ilock(&bighash);
	l = &bighash.set[BIGHASH(p)];
	for(; (b = *l) != nil; l = &b->next)
		if(b->va == (uintptr_t)p){
			used = b->nbytes;
			*l = b->next;
			b->next = bighash.free;
			bighash.free = b;
			iunlock(&bighash);
			basefree(p, used);
			return;
		}
	iunlock(&bighash);
	panic("bigfree");
}

typedef struct Rov Rov;
struct Rov{
	uint32_t	tag;
	uint32_t	size;
};
static Rov rovers[2048];

void
mallocsummary(void)
{
	Header *q;
	int i, n, t;
	Qlist *qlist;

	t = 0;
	for(i = 0; i <= NQUICK; i++) {
		n = 0;
		qlist = &quicklist[i];
		ilock(&qlist->lk);
		for(q = qlist->first; q != nil; q = q->s.next){
			if(q->s.size != i)
				DBG("q%d\t%#p\t%ud\n", i, q, q->s.size);
			n++;
		}
		iunlock(&qlist->lk);

		t += n * i*Unitsz;
	}
	print("quick: %ud bytes total\n", t);

	MLOCK;
	if((q = rover) != nil){
		i = t = 0;
		do {
			t += q->s.size;
			if(i < nelem(rovers)){
				rovers[i].tag = q->s.tag;
				rovers[i].size = q->s.size;
			}
			i++;
		} while((q = q->s.next) != rover);
	}
	MUNLOCK;

	if(i != 0){
		print("rover: %d blocks %ud bytes total\n",
			i, t*Unitsz);
		while(--i >= 0)
			if(i < nelem(rovers) && rovers[i].size != 0)
				print("R%d: %#8.8ux %ud\n", i, rovers[i].tag, rovers[i].size);
	}

	for(i = 0; i < nelem(qstats); i++){
		if(qstats[i] == 0)
			continue;
		print("%s: %ud\n", qstatname[i], qstats[i]);
	}
}

void
free(void* ap)
{
	Header *h;
	BigAlloc *b;
	usize nunits;

	if(ap == nil)
		return;
	qstats[QSfree]++;
	if(isbigalloc(ap)){
		b = bigfind(ap);
		if(b != nil){
			bigfree(ap);
			return;
		}
	}
	h = D2H(ap);
	nunits = h->s.size;
	if((nunits & Busy) == 0)
		panic("free: already free %#p: freed %#p tag %#ux", ap, getcallerpc(), h->s.tag);
	nunits &= ~Busy;
	h->s.size = nunits;
	if(nunits < MinUnits)
		panic("free: empty block: corrupt allocation arena");
	if(memprof != nil)
		memprof(ap, h->s.tag, (nunits-1)*Unitsz, -1);
	if(Poisoning)
		memset(h+1, 0xAA, (nunits-1)*Unitsz);
	if(!freetail(h)){
		if(nunits > NQUICK){
			MLOCK;
			freemisc(h);
			MUNLOCK;
		}else
			freequick(h);
	}
}

void*
qmalloc(uint32_t size)
{
	void* v;

	qstats[QSmalloc]++;
if(size > 1536*1024)print("qmalloc %lud %#p\n", size, getcallerpc());
	if(size >= BigThreshold)
		v = bigalloc(size, 0, getcallerpc());
	else
		v = qalloc(size, getcallerpc());
	return v;
}

void*
malloc(uint32_t size)
{
	void* v;

	qstats[QSmalloc]++;
if(size > 1536*1024)print("malloc %lud %#p\n", size, getcallerpc());
	if(size >= BigThreshold)
		v = bigalloc(size, 0, getcallerpc());
	else
		v = qalloc(size, getcallerpc());
	if(v != nil)
		memset(v, 0, size);
	return v;
}

void*
mallocz(uint32_t size, int clr)
{
	void *v;

	qstats[QSmalloc]++;
if(size > 1900*1024)print("mallocz %lud %#p\n", size, getcallerpc());
	if(size >= BigThreshold)
		v = bigalloc(size, 0, getcallerpc());
	else
		v = qalloc(size, getcallerpc());
	if(v == nil)
		return nil;
	if(clr)
		memset(v, 0, size);
	return v;
}

void*
mallocalign(uint32_t nbytes, uint32_t align, long offset, uint32_t span)
{
	void *v;

	qstats[QSmalloc]++;
	if(span != 0 && align <= span){
		if(nbytes > span)
			return nil;
		align = span;
		span = 0;
	}
	if(align <= Align)
		return mallocz(nbytes, 1);

if(nbytes > 1900*1024)print("mallocalign %lud %lud %#p\n", nbytes, align, getcallerpc());

	if(nbytes >= BigThreshold)
		v = bigalloc(nbytes, align, getcallerpc());
	else
		v = qallocalign(nbytes, align,offset, span, getcallerpc());
	if(v != nil){
		if(align && (uintptr_t)v & (align-1))
			panic("mallocalign %#p %#lux", v, align);
		memset(v, 0, nbytes);	/* leave it to caller? */
	}
	return v;
}

void*
sqmalloc(uint32_t size)
{
	void *v;

	while((v = malloc(size)) == nil)
		tsleep(&up->sleep, return0, 0, 100);
	setmalloctag(v, getcallerpc());

	return v;
}

void*
smalloc(uint32_t size)
{
	void *v;

	while((v = malloc(size)) == nil)
		tsleep(&up->sleep, return0, 0, 100);
	setmalloctag(v, getcallerpc());
	memset(v, 0, size);

	return v;
}

void*
realloc(void* ap, uint32_t size)
{
	void *v;
	Header *h;
	BigAlloc *b;
	uint32_t osize;
	uint32_t nunits, ounits;
	int delta;
	Region *t;

	/*
	 * Easy stuff:
	 * free and return nil if size is 0
	 * (implementation-defined behaviour);
	 * behave like malloc if ap is nil;
	 * check for arena corruption;
	 * do nothing if units are the same.
	 */
	if(size == 0){
		free(ap);
		return nil;
	}
	if(ap == nil){
		v = malloc(size);
		if(v != nil)
			setmalloctag(v, getcallerpc());
		return v;
	}

	if(!isbigalloc(ap) || (b = bigfind(ap)) == nil){
		h = D2H(ap);
		ounits = h->s.size & ~Busy;
		if((h->s.size & Busy) == 0 || ounits == 0)
			panic("realloc: corrupt allocation arena");

		if((nunits = NUNITS(size)) == ounits)
			return ap;

		/*
		 * Slightly harder:
		 * if this allocation abuts the tail of a region, try to adjust the tail
		 */
		MLOCK;
		for(t = tail; t != nil; t = t->down){
			if(t->tail != nil && h+ounits == t->tail){
				delta = nunits-ounits;
				if(delta < 0 || t->size >= delta){
					h->s.size = nunits | Busy;
					t->size -= delta;
					t->tail += delta;
					MUNLOCK;
					return ap;
				}
			}
		}
		MUNLOCK;
		osize = (ounits-1)*Unitsz;
	}else
		osize = b->nbytes;

	/*
	 * Too hard (or can't be bothered):
	 * allocate, copy and free.
	 * The original block must be unchanged on failure.
	 */
	if((v = malloc(size)) != nil){
		setmalloctag(v, getcallerpc());
		if(size < osize)
			osize = size;
		memmove(v, ap, osize);
		free(ap);
	}

	return v;
}

void
setmalloctag(void *a, uint32_t tag)
{
	Header *h;
	BigAlloc *b;

	if(isbigalloc(a) && (b = bigfind(a)) != nil){
		b->tag = tag;
		if(memprof != nil)
			memprof(a, tag, b->nbytes, 2);
		return;
	}
	h = D2H(a);
	if((h->s.size & Busy) == 0)
		panic("setmalloctag free %#p %#lux [%#ux %#ux] %#p", a, tag, h->s.size, h->s.tag, getcallerpc());
	h->s.tag = tag;
	if(memprof != nil)
		memprof(a, tag, (h->s.size-1)*Unitsz, 2);
}

uint32_t
getmalloctag(void *a)
{
	BigAlloc *b;

	if(a == nil)
		return 0;
	if(isbigalloc(a) && (b = bigfind(a)) != nil)
		return b->tag;
	return D2H(a)->s.tag;
}

void
mallocinit(void)
{
	BigAlloc *p, *pe;

	if(tail != nil)
		return;

	rover = &misclist;
	rover->s.next = rover;
	pe = &bighash.pool[nelem(bighash.pool)-1];
	bighash.free = p = bighash.pool;
	for(; p < pe; p++)
		p->next = p+1;
	p->next = nil;
	if(!morecore(BigThreshold/Unitsz))
		panic("mallocinit");
	print("base %#p bound %#p nunits %lud\n", tail->start, tail->end, tail->end - tail->start);
}

/*
 * get some space from basealloc
 */
static int
morecore(uint32_t nunits)
{
	usize nbytes;
	char *p;
	Region *r;

	if(nunits < NUNITS(256*KiB))
		nunits = NUNITS(256*KiB);
	nbytes = nunits*Unitsz + sizeof(Region) + Unitsz;
	p = basealloc(nbytes, Align, &nbytes);
	if(p == nil)
		return 0;
	/* build a new region if current one can't be extended */
	if((r = tail) == nil || p != r->limit){
		r = (Region*)p;
		r->base = p;
		r->start= ALIGNHDR(r+1, Align);
		r->tail = r->start;
		r->down = tail;
		tail = r;
	}
	r->limit = p+nbytes;
	r->end = r->start + (r->limit - (char*)r->start)/Unitsz;
	r->size = r->end - r->tail;
	return 1;
}

void
setmemprof(void (*f)(void*, uint32_t, usize, int))
{
	memprof = f;
}

/*
 * Mstate :: base  (Used|Free Size)*
 */
void
snapmemarena(void)
{
	/* just the available parts */
}
