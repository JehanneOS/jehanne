/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct Mprof Mprof;
typedef struct Mevents Mevents;
typedef struct Mevent Mevent;

enum
{
	Qdir,
	Qctl,
	Qevent,
	Qprof,
	Qsum,
};

static
Dirtab memdir[] =
{
	".",			{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"memctl",		{Qctl},	0,			0664,
	"memevent",	{Qevent},	0,			0444,
	"memprof",	{Qprof},	0,			0444,
	"memsum",	{Qsum},	0,			0444,
};

enum
{
	Nevent = 10000,
	BucketLg2=	15,	/* for 512k kernel, allows allocation every 16 bytes */
	Nbucket=	1<<BucketLg2,
	BucketMask=	Nbucket-1,
	Noverflow=	100,	/* number of entries for duplicates */
	Profreclen=	3*4,	/* tag, na, busy */
	Evreclen=		4*4,	/* 0, tag, koff, size */

	MaxInt=	0x7FFFFFFF,
};

/*
 * allocation profile
 */
struct Mprof{
	uint32_t	tag;		/* usually low-order 32 bits of pc */
	int	na;			/* active allocations */
	int	busy;		/* bytes currently allocated */
	uint32_t	ovfl;			/* !=0, next index on overflow */
};

static struct
{
	Mprof	bucket[Nbucket+Noverflow+1];	/* last entry as catchall */
	int	novfl;
	Lock	ovlk;
} memprof;

/*
 * allocation events
 */
struct Mevent
{
	uint32_t	tag;		/* usually low-order 32 bits of pc */
	uint32_t	koff;		/* base-KZERO */
	int	size;		/* > 0, alloc; < 0, free */
};

static struct Mevents
{
	Lock;
	Ref;
	Rendez	r;
	Mevent	events[Nevent];
	uint32_t	rd;
	uint32_t	wr;
	int	want;
	uint32_t	lost;
} memevents;

static	Ref	monitoring;

extern	void setmemprof(void (*)(void*, uint32_t, usize, int));	/* qmalloc.c */

static void
aadd(int *addr, int delta)
{
	int value;

	do
		value = *addr;
	while(!CASW(addr, value, value+delta));
}

static int
isnonempty(void *v)
{
	Mevents *evs;

	evs = v;
	return evs->rd != evs->wr;
}

static int
isnotfull(Mevents *evs)
{
	return (evs->wr - evs->rd) < Nevent;
}

static void
addmemevent(void *a, uint32_t tag, usize nb, int w)
{
	Mevents *evs;
	Mevent e;
	int empty;

	e.tag = tag;
	e.koff = (uintptr_t)a - KZERO;
	if(nb > MaxInt)
		nb = MaxInt;
	e.size = w < 0? -nb: nb;

	evs = &memevents;
	ilock(evs);
	if(isnotfull(evs)){
		empty = evs->rd == evs->wr;
		evs->events[evs->wr++] = e;
	}else{
		evs->lost++;
		empty = 0;
	}
	iunlock(evs);
	if(empty)
		wakeup(&evs->r);
}

static void
mprofmonitor(void *a, uint32_t tag, usize nb, int w)
{
	Mprof *p;
	uint32_t n;

	if(memevents.ref != 0)
		addmemevent(a, tag, nb, w);
	n = ((tag-(KTZERO&0xFFFFFFFF))/(512*KiB/Nbucket))&BucketMask;
	if(n > Nbucket)
		n = Nbucket-1;
	for(;;){
		p = &memprof.bucket[n];
		if(p->tag == tag || p->tag == ~0)
			break;
		n = p->ovfl;
		if(n == 0){
			if(w < 0)
				return;
			ilock(&memprof.ovlk);
			if(p->tag != 0 && p->tag != tag){
				n = p->ovfl;
				if(n != 0){
					iunlock(&memprof.ovlk);
					/* follow the overflow chain */
					continue;
				}
				/* need an overflow entry */
				n = memprof.novfl;
				if(n < Noverflow)
					memprof.novfl++;
				else
					tag = ~0;
				n += Nbucket;
				p->ovfl = n;
				p = &memprof.bucket[n];
			}
			p->tag = tag;
			iunlock(&memprof.ovlk);
			break;
		}
	}
	if(w < 0){
		aadd(&p->na, -1);
		aadd(&p->busy, -nb);
	}else{
		aadd(&p->na, 1);
		aadd(&p->busy, nb);
	}
}

static void
mput4(uint8_t *m, uint32_t v)
{
	m[0] = v>>24;
	m[1] = v>>16;
	m[2] = v>>8;
	m[3] = v;
}

static void
memprofinit(void)
{
	incref(&monitoring);
	setmemprof(mprofmonitor);
}

static Chan*
memattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('%', spec);
}

static Walkqid*
memwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, memdir, nelem(memdir), devgen);
}

static long
memstat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, memdir, nelem(memdir), devgen);
}

static Chan*
memopen(Chan *c, int omode)
{
	Mevents *evs;

	c = devopen(c, omode, memdir, nelem(memdir), devgen);
	switch((uint32_t)c->qid.path){
	case Qevent:
		evs = &memevents;
		if(incref(evs) != 1){
			decref(evs);
			c->flag &= ~COPEN;
			error(Einuse);
		}
		evs->rd = evs->wr = 0;
		evs->want = 0;
		evs->lost = 0;
		incref(&monitoring);
		setmemprof(mprofmonitor);
		break;
	case Qprof:
		break;
	}
	return c;
}

static void
memclose(Chan *c)
{
	if((c->flag & COPEN) == 0)
		return;
	switch((uint32_t)c->qid.path) {
	case Qevent:
		if(decref(&monitoring) == 0)
			setmemprof(nil);
		decref(&memevents);
		break;
	case Qprof:
		break;
	}

}

static long
memread(Chan *c, void *va, long count, int64_t offset)
{
	uint8_t *a;
	int i;
	Mevent *pe;
	Mevents *evs;
	Mprof *p;

	if(c->qid.type & QTDIR)
		return devdirread(c, va, count, memdir, nelem(memdir), devgen);

	switch((uint32_t)c->qid.path) {
	default:
		error(Egreg);
	case Qctl:
		return 0;
	case Qsum:
		return mallocreadsummary(c, va, count, offset);
	case Qevent:
		evs = &memevents;
		while(!isnonempty(evs)){
			evs->want = 1;
			sleep(&evs->r, isnonempty, evs);
		}
		a = va;
		do{
			if((count -= Evreclen) < 0)
				break;
			pe = &evs->events[evs->rd];
			mput4(a+0, 0);
			mput4(a+4, pe->tag);
			mput4(a+8, pe->koff);
			mput4(a+12, pe->size);
			a += Evreclen;
		}while(++evs->rd != evs->wr);
		return a-(uint8_t*)va;
	case Qprof:
		a = va;
		for(i = offset/Profreclen; i < nelem(memprof.bucket); i++){
			p = &memprof.bucket[i];
			if((p->tag|p->na|p->busy) != 0){
				if((count -= Profreclen) < 0)
					break;
				mput4(a+0, p->tag);
				mput4(a+4, p->na);
				mput4(a+8, p->busy);
				a += Profreclen;
			}
		}
		return a-(uint8_t*)va;
	}
}

static long
memwrite(Chan *c, void *a, long n, int64_t)
{
	Cmdbuf *cb;

	if(c->qid.type & QTDIR)
		error(Eperm);

	switch((uint32_t)c->qid.path) {
	default:
		error(Egreg);
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			jehanne_free(cb);
			nexterror();
		}
		if(cb->nf == 1 && jehanne_strcmp(cb->f[0], "start") == 0){
			if(incref(&monitoring) == 1)
				setmemprof(mprofmonitor);
		}else if(cb->nf == 1 && jehanne_strcmp(cb->f[0], "stop") == 0){
			if(decref(&monitoring) == 0)
				setmemprof(nil);
		}else
			cmderror(cb, "unknown command");
		poperror();
		jehanne_free(cb);
		break;
	}
	return n;
}

Dev memdevtab = {
	'%',
	"mem",

	devreset,
	memprofinit,	//devinit,
	devshutdown,
	memattach,
	memwalk,
	memstat,
	memopen,
	devcreate,
	memclose,
	memread,
	devbread,
	memwrite,
	devbwrite,
	devremove,
	devwstat
};
