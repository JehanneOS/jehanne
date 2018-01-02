/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"
#include	<pool.h>

static void poolprint(Pool*, char*, ...);
static void ppanic(Pool*, char*, ...);
static void pool_lock(Pool*);
static void pool_unlock(Pool*);

typedef struct Private	Private;
struct Private {
	Lock		lk;
	char		msg[256];	/* a rock for messages to be printed at unlock */
};

static Private pmainpriv;
static Pool pmainmem = {
	.name=	"Main",
	.maxsize=	4*1024*1024,
	.minarena=	128*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	POOL_TOLERANCE,

	.lock=	pool_lock,
	.unlock=	pool_unlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&pmainpriv,
};

static Private pimagpriv;
static Pool pimagmem = {
	.name=	"Image",
	.maxsize=	16*1024*1024,
	.minarena=	2*1024*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	0,

	.lock=	pool_lock,
	.unlock=	pool_unlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&pimagpriv,
};

static Private psecrpriv;
static Pool psecrmem = {
	.name=	"Secrets",
	.maxsize=	16*1024*1024,
	.minarena=	64*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	POOL_ANTAGONISM,

	.lock=	pool_lock,
	.unlock=	pool_unlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&psecrpriv,
};

Pool*	mainmem = &pmainmem;
Pool*	imagmem = &pimagmem;
Pool*	secrmem = &psecrmem;

/*
 * because we can't print while we're holding the locks,
 * we have the save the message and print it once we let go.
 */
static void
poolprint(Pool *p, char *fmt, ...)
{
	va_list v;
	Private *pv;

	pv = p->private;
	va_start(v, fmt);
	vseprint(pv->msg+strlen(pv->msg), pv->msg+sizeof pv->msg, fmt, v);
	va_end(v);
}

static void
ppanic(Pool *p, char *fmt, ...)
{
	va_list v;
	Private *pv;
	char msg[sizeof pv->msg];

	pv = p->private;
	va_start(v, fmt);
	vseprint(pv->msg+strlen(pv->msg), pv->msg+sizeof pv->msg, fmt, v);
	va_end(v);
	memmove(msg, pv->msg, sizeof msg);
	iunlock(&pv->lk);
	panic("%s", msg);
}

static void
pool_lock(Pool *p)
{
	Private *pv;

	pv = p->private;
	ilock(&pv->lk);
//	pv->lk.pc = getcallerpc();
	pv->msg[0] = 0;
}

static void
pool_unlock(Pool *p)
{
	Private *pv;
	char msg[sizeof pv->msg];

	pv = p->private;
	if(pv->msg[0] == 0){
		iunlock(&pv->lk);
		return;
	}

	memmove(msg, pv->msg, sizeof msg);
	iunlock(&pv->lk);
	iprint("%.*s", sizeof pv->msg, msg);
}

void
poolsummary(Pool *p)
{
	print("%s max %llud cur %llud free %llud alloc %llud\n", p->name,
		(uint64_t)p->maxsize, (uint64_t)p->cursize,
		(uint64_t)p->curfree, (uint64_t)p->curalloc);
}

void
mallocsummary(void)
{
	poolsummary(mainmem);
	poolsummary(imagmem);
	poolsummary(secrmem);
}

/* everything from here down should be the same in libc, libdebugmalloc, and the kernel */
/* - except the code for malloc(), which alternately doesn't clear or does. */
/* - except the code for smalloc(), which lives only in the kernel. */

/*
 * Npadlong is the number of uint32_t's to leave at the beginning of
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

/*	non tracing
 *
enum {
	Npadlong	= 0,
	MallocOffset = 0,
	ReallocOffset = 0,
};
 *
 */

/* tracing */
enum {
	Npadlong	= 2,
	MallocOffset = 0,
	ReallocOffset = 1
};


void*
smalloc(uint32_t size)
{
	void *v;

	while((v = poolalloc(mainmem, size+Npadlong*sizeof(uint32_t))) == nil){
		if(!waserror()){
			resrcwait(nil, nil);
			poperror();
		}
	}
	if(Npadlong){
		v = (uint32_t*)v+Npadlong;
		setmalloctag(v, getcallerpc());
	}
	memset(v, 0, size);
	return v;
}

void*
jehanne_malloc(uint32_t size)
{
	void *v;

	v = poolalloc(mainmem, size+Npadlong*sizeof(uint32_t));
	if(v == nil)
		return nil;
	if(Npadlong){
		v = (uint32_t*)v+Npadlong;
		setmalloctag(v, getcallerpc());
		setrealloctag(v, 0);
	}
	memset(v, 0, size);
	return v;
}

void*
jehanne_mallocz(uint32_t size, int clr)
{
	void *v;

	v = poolalloc(mainmem, size+Npadlong*sizeof(uint32_t));
	if(Npadlong && v != nil){
		v = (uint32_t*)v+Npadlong;
		setmalloctag(v, getcallerpc());
		setrealloctag(v, 0);
	}
	if(clr && v != nil)
		memset(v, 0, size);
	return v;
}

void*
jehanne_mallocalign(uint32_t size, uint32_t align, long offset, uint32_t span)
{
	void *v;

	v = poolallocalign(mainmem, size+Npadlong*sizeof(uint32_t), align, offset-Npadlong*sizeof(uint32_t), span);
	if(Npadlong && v != nil){
		v = (uint32_t*)v+Npadlong;
		setmalloctag(v, getcallerpc());
		setrealloctag(v, 0);
	}
	if(v)
		memset(v, 0, size);
	return v;
}

void
jehanne_free(void *v)
{
	if(v != nil)
		poolfree(mainmem, (uint32_t*)v-Npadlong);
}

void*
jehanne_realloc(void *v, uint32_t size)
{
	void *nv;

	if(v != nil)
		v = (uint32_t*)v-Npadlong;
	if(Npadlong !=0 && size != 0)
		size += Npadlong*sizeof(uint32_t);

	if(nv = poolrealloc(mainmem, v, size)){
		nv = (uint32_t*)nv+Npadlong;
		setrealloctag(nv, getcallerpc());
		if(v == nil)
			setmalloctag(nv, getcallerpc());
	}
	return nv;
}

uint32_t
jehanne_msize(void *v)
{
	return poolmsize(mainmem, (uint32_t*)v-Npadlong)-Npadlong*sizeof(uint32_t);
}

/* secret memory, used to back cryptographic keys and cipher states */
void*
secalloc(uint32_t size)
{
	void *v;

	while((v = poolalloc(secrmem, size+Npadlong*sizeof(uint32_t))) == nil){
		if(!waserror()){
			resrcwait(nil, nil);
			poperror();
		}
	}
	if(Npadlong){
		v = (uint32_t*)v+Npadlong;
		setmalloctag(v, getcallerpc());
		setrealloctag(v, 0);
	}
	memset(v, 0, size);
	return v;
}

void
secfree(void *v)
{
	if(v != nil)
		poolfree(secrmem, (uint32_t*)v-Npadlong);
}

void
jehanne_setmalloctag(void *v, uintptr_t pc)
{
	USED(v, pc);
	if(Npadlong <= MallocOffset || v == nil)
		return;
	((uint32_t*)v)[-Npadlong+MallocOffset] = (uint32_t)pc;
}

void
jehanne_setrealloctag(void *v, uintptr_t pc)
{
	USED(v, pc);
	if(Npadlong <= ReallocOffset || v == nil)
		return;
	((uint32_t*)v)[-Npadlong+ReallocOffset] = (uint32_t)pc;
}

uintptr_t
jehanne_getmalloctag(void *v)
{
	USED(v);
	if(Npadlong <= MallocOffset)
		return ~0;
	return (int)((uint32_t*)v)[-Npadlong+MallocOffset];
}

uintptr_t
jehanne_getrealloctag(void *v)
{
	USED(v);
	if(Npadlong <= ReallocOffset)
		return ~0;
	return (int)((uint32_t*)v)[-Npadlong+ReallocOffset];
}
