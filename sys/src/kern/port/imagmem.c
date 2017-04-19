/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include	<pool.h>

static void poolprint(Pool*, char*, ...);
static void ppanic(Pool*, char*, ...);
static void plock(Pool*);
static void punlock(Pool*);

typedef struct Private	Private;
struct Private {
	Lock		lk;
	char*		end;
	char		msg[256];	/* a rock for messages to be printed at unlock */
};

static Private pimagpriv;
static Pool pimagmem = {
	.name=		"Image",
	.maxsize=	16*1024*1024,
	.minarena=	2*1024*1024,
	.quantum=	32,
	.alloc=		qmalloc,
	.merge=		nil,
	.flags=		0,

	.lock=		plock,
	.unlock=	punlock,
	.print=		poolprint,
	.panic=		ppanic,

	.private=	&pimagpriv,
};

Pool*	imagmem = &pimagmem;

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
	pv->end = jehanne_vseprint(pv->end, &pv->msg[sizeof pv->msg], fmt, v);
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
	jehanne_vseprint(pv->end, &pv->msg[sizeof pv->msg], fmt, v);
	va_end(v);
	jehanne_memmove(msg, pv->msg, sizeof msg);
	iunlock(&pv->lk);
	panic("%s", msg);
}

static void
plock(Pool *p)
{
	Private *pv;

	pv = p->private;
	ilock(&pv->lk);
	//pv->lk.pc = getcallerpc();	//  TODO: mcslocks handles pc differently, add this back introducing ilockat(l, pc)
	pv->end = pv->msg;
}

static void
punlock(Pool *p)
{
	Private *pv;
	char msg[sizeof pv->msg];

	pv = p->private;
	if(pv->end == pv->msg){
		iunlock(&pv->lk);
		return;
	}

	jehanne_memmove(msg, pv->msg, sizeof msg);
	pv->end = pv->msg;
	iunlock(&pv->lk);
	iprint("%.*s", sizeof pv->msg, msg);
}
