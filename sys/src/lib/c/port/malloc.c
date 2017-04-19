/*
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <pool.h>

static void*	sbrkalloc(uint32_t);
static int		sbrkmerge(void*, void*);
static void		plock(Pool*);
static void		punlock(Pool*);
static void		pprint(Pool*, char*, ...);
static void		ppanic(Pool*, char*, ...);

typedef struct Private Private;
struct Private {
	Lock		lk;
	int32_t		pid;
	int32_t		printfd;	/* gets debugging output if set */
};

Private sbrkmempriv;

static Pool sbrkmem = {
	.name=		"sbrkmem",
	.maxsize=	(3840UL-1)*1024*1024,	/* up to ~0xf0000000 */
	.minarena=	4*1024,
	.quantum=	32,
	.alloc=		sbrkalloc,
	.merge=		sbrkmerge,
	.flags=		0,

	.lock=		plock,
	.unlock=		punlock,
	.print=		pprint,
	.panic=		ppanic,
	.private=		&sbrkmempriv,
};
Pool *mainmem = &sbrkmem;
Pool *imagmem = &sbrkmem;

/*
 * we do minimal bookkeeping so we can tell pool
 * whether two blocks are adjacent and thus mergeable.
 */
static void*
sbrkalloc(uint32_t n)
{
	uint32_t *x;

	n += 2*sizeof(uint32_t);	/* two longs for us */
	x = sbrk(n);
	if(x == (void*)-1)
		return nil;
	x[0] = (n+7)&~7;	/* sbrk rounds size up to mult. of 8 */
	x[1] = 0xDeadBeef;
	return x+2;
}

static int
sbrkmerge(void *x, void *y)
{
	uint32_t *lx, *ly;

	lx = x;
	if(lx[-1] != 0xDeadBeef)
		abort();

	if((uint8_t*)lx+lx[-2] == (uint8_t*)y) {
		ly = y;
		lx[-2] += ly[-2];
		return 1;
	}
	return 0;
}

static void
plock(Pool *p)
{
	Private *pv;
	pv = p->private;
	jehanne_lock(&pv->lk);
	if(pv->pid != 0)
		abort();
	pv->pid = jehanne_getmainpid();
}

static void
punlock(Pool *p)
{
	Private *pv;
	pv = p->private;
	if(pv->pid != jehanne_getmainpid())
		abort();
	pv->pid = 0;
	jehanne_unlock(&pv->lk);
}

static int
checkenv(void)
{
	int n, fd;
	char buf[20];
	fd = open("/env/MALLOCFD", OREAD);
	if(fd < 0)
		return -1;
	if((n = read(fd, buf, sizeof buf)) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	if(n >= sizeof buf)
		n = sizeof(buf)-1;
	buf[n] = 0;
	n = jehanne_atoi(buf);
	if(n == 0)
		n = -1;
	return n;
}

static void
pprint(Pool *p, char *fmt, ...)
{
	va_list v;
	Private *pv;

	pv = p->private;
	if(pv->printfd == 0)
		pv->printfd = checkenv();

	if(pv->printfd <= 0)
		pv->printfd = 2;

	va_start(v, fmt);
	jehanne_vfprint(pv->printfd, fmt, v);
	va_end(v);
}

static char panicbuf[256];
static void
ppanic(Pool *p, char *fmt, ...) 
{
	va_list v;
	int n;
	char *msg;
	Private *pv;

	pv = p->private;
	assert(jehanne_canlock(&pv->lk)==0);

	if(pv->printfd == 0)
		pv->printfd = checkenv();
	if(pv->printfd <= 0)
		pv->printfd = 2;

	msg = panicbuf;
	va_start(v, fmt);
	n = jehanne_vseprint(msg, msg+sizeof panicbuf, fmt, v) - msg;
	write(2, "panic: ", 7);
	write(2, msg, n);
	write(2, "\n", 1);
	if(pv->printfd != 2){
		write(pv->printfd, "panic: ", 7);
		write(pv->printfd, msg, n);
		write(pv->printfd, "\n", 1);
	}
	va_end(v);
//	jehanne_unlock(&pv->lk);
	abort();
}

/* - everything from here down should be the same in libc, libdebugmalloc, and the kernel - */
/* - except the code for jehanne_malloc(), which alternately doesn't clear or does. - */

/*
 * Npadlong is the number of uintptrs to leave at the beginning of 
 * each allocated buffer for our own bookkeeping.  We return to the callers
 * a pointer that points immediately after our bookkeeping area.  Incoming pointers
 * must be decremented by that much, and outgoing pointers incremented.
 * The malloc tag is stored at MallocOffset from the beginning of the block,
 * and the realloc tag at ReallocOffset.  The offsets are from the true beginning
 * of the block, not the beginning the caller sees.
 *
 * The extra if(Npadlong != 0) in various places is a hint for the compiler to
 * compile out function calls that would otherwise be no-ops.
 */

/* tracing */
enum {
	Npadlong	= 2,
	MallocOffset = 0,
	ReallocOffset = 1
};

void*
jehanne_malloc(size_t size)
{
	void *v;

	v = poolalloc(mainmem, size+Npadlong*sizeof(uintptr_t));
	if(Npadlong && v != nil) {
		v = (uintptr_t*)v+Npadlong;
		jehanne_setmalloctag(v, jehanne_getcallerpc());
		jehanne_setrealloctag(v, 0);
	}
	return v;
}

void*
jehanne_mallocz(uint32_t size, int clr)
{
	void *v;

	v = poolalloc(mainmem, size+Npadlong*sizeof(uintptr_t));
	if(Npadlong && v != nil){
		v = (uintptr_t*)v+Npadlong;
		jehanne_setmalloctag(v, jehanne_getcallerpc());
		jehanne_setrealloctag(v, 0);
	}
	if(clr && v != nil)
		jehanne_memset(v, 0, size);
	return v;
}

void*
jehanne_mallocalign(uint32_t size, uint32_t align, int32_t offset, uint32_t span)
{
	void *v;

	v = poolallocalign(mainmem, size+Npadlong*sizeof(uintptr_t), align,
			   offset-Npadlong*sizeof(uintptr_t), span);
	if(Npadlong && v != nil){
		v = (uintptr_t*)v+Npadlong;
		jehanne_setmalloctag(v, jehanne_getcallerpc());
		jehanne_setrealloctag(v, 0);
	}
	return v;
}

void
jehanne_free(void *v)
{
	if(v != nil)
		poolfree(mainmem, (uintptr_t*)v-Npadlong);
}

void*
jehanne_realloc(void *v, size_t size)
{
	void *nv;

	if(size == 0){
		jehanne_free(v);
		return nil;
	}

	if(v)
		v = (uintptr_t*)v-Npadlong;
	size += Npadlong*sizeof(uintptr_t);

	if((nv = poolrealloc(mainmem, v, size))){
		nv = (uintptr_t*)nv+Npadlong;
		jehanne_setrealloctag(nv, jehanne_getcallerpc());
		if(v == nil)
			jehanne_setmalloctag(nv, jehanne_getcallerpc());
	}		
	return nv;
}

uint32_t
jehanne_msize(void *v)
{
	return poolmsize(mainmem, (uintptr_t*)v-Npadlong)-Npadlong*sizeof(uintptr_t);
}

void*
jehanne_calloc(uint32_t n, size_t szelem)
{
	void *v;
	
	if(n > 1 && ((uint64_t)-1)/n < szelem)
		return nil;
	if((v = jehanne_mallocz(n*szelem, 1)))
		jehanne_setmalloctag(v, jehanne_getcallerpc());
	return v;
}

void
jehanne_setmalloctag(void *v, uintptr_t pc)
{
	if(Npadlong <= MallocOffset || v == nil)
		return;
	((uintptr_t*)v)[-Npadlong+MallocOffset] = pc;
}

void
jehanne_setrealloctag(void *v, uintptr_t pc)
{
	if(Npadlong <= ReallocOffset || v == nil)
		return;
	((uintptr_t*)v)[-Npadlong+ReallocOffset] = pc;
}

uintptr_t
jehanne_getmalloctag(void *v)
{
	if(Npadlong <= MallocOffset)
		return ~0;
	return ((uintptr_t*)v)[-Npadlong+MallocOffset];
}

uintptr_t
jehanne_getrealloctag(void *v)
{
	if(Npadlong <= ReallocOffset)
		return ~0;
	return ((uintptr_t*)v)[-Npadlong+ReallocOffset];
}

void*
jehanne_malloctopoolblock(void *v)
{
	if(v == nil)
		return nil;

	return &((uintptr_t*)v)[-Npadlong];
}
