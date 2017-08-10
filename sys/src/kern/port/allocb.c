#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Tlrspc		= 16,		/* extra room at the end for pad/crc/mac */
	Bdead		= 0x51494F42,	/* "QIOB" */
};

static Block*
_allocb(int size)
{
	Block *b;
	uintptr_t addr;

	size += Tlrspc;
	if((b = mallocz(sizeof(Block)+size+Hdrspc, 0)) == nil)
		return nil;

	b->next = nil;
	b->list = nil;
	b->free = nil;
	b->flag = 0;

	/* align start of data portion by rounding up */
	addr = (uintptr_t)b;
	addr = ROUND(addr + sizeof(Block), BLOCKALIGN);
	b->base = (uint8_t*)addr;

	/* align end of data portion by rounding down */
	b->lim = (uint8_t*)b + msize(b);
	addr = (uintptr_t)b->lim;
	addr &= ~(BLOCKALIGN-1);
	b->lim = (uint8_t*)addr;

	/* leave sluff at beginning for added headers */
	b->rp = b->lim - ROUND(size, BLOCKALIGN);
	if(b->rp < b->base)
		panic("_allocb");
	b->wp = b->rp;

	return b;
}

Block*
allocb(int size)
{
	Block *b;

	/*
	 * Check in a process and wait until successful.
	 */
	if(up == nil)
		panic("allocb without up: %#p", getcallerpc());
	while((b = _allocb(size)) == nil){
		if(up->nlocks || m->ilockdepth || !islo()){
			xsummary();
			mallocsummary();
			panic("allocb: no memory for %d bytes", size);
		}
		if(!waserror()){
			resrcwait("no memory for allocb", nil);
			poperror();
		}
	}
	setmalloctag(b, getcallerpc());

	return b;
}

Block*
iallocb(int size)
{
	Block *b;

	if((b = _allocb(size)) == nil){
		static uint32_t nerr;
		if((nerr++%10000)==0){
			if(nerr > 10000000){
				xsummary();
				mallocsummary();
				panic("iallocb: out of memory");
			}
			iprint("iallocb: no memory for %d bytes\n", size);
		}
		return nil;
	}
	setmalloctag(b, getcallerpc());
	b->flag = BINTR;

	return b;
}

void
freeb(Block *b)
{
	void *dead = (void*)Bdead;

	if(b == nil)
		return;

	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if(b->free != nil) {
		b->free(b);
		return;
	}

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	free(b);
}

void
checkb(Block *b, char *msg)
{
	void *dead = (void*)Bdead;

	if(b == dead)
		panic("checkb b %s %#p", msg, b);
	if(b->base == dead || b->lim == dead || b->next == dead
	  || b->rp == dead || b->wp == dead){
		print("checkb: base %#p lim %#p next %#p\n",
			b->base, b->lim, b->next);
		print("checkb: rp %#p wp %#p\n", b->rp, b->wp);
		panic("checkb dead: %s", msg);
	}

	if(b->base > b->lim)
		panic("checkb 0 %s %#p %#p", msg, b->base, b->lim);
	if(b->rp < b->base)
		panic("checkb 1 %s %#p %#p", msg, b->base, b->rp);
	if(b->wp < b->base)
		panic("checkb 2 %s %#p %#p", msg, b->base, b->wp);
	if(b->rp > b->lim)
		panic("checkb 3 %s %#p %#p", msg, b->rp, b->lim);
	if(b->wp > b->lim)
		panic("checkb 4 %s %#p %#p", msg, b->wp, b->lim);
}
