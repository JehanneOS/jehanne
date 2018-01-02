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


#define LRES	3		/* log of PC resolution */
#define SZ	4		/* sizeof of count cell; well known as 4 */

struct
{
	uintptr_t	minpc;
	uintptr_t	maxpc;
	int	nbuf;
	int	time;
	uint32_t	*buf;
}kprof;

enum{
	Kprofdirqid,
	Kprofdataqid,
	Kprofctlqid,
};
Dirtab kproftab[]={
	".",		{Kprofdirqid, 0, QTDIR},0,	DMDIR|0550,
	"kpdata",	{Kprofdataqid},		0,	0600,
	"kpctl",	{Kprofctlqid},		0,	0600,
};

static void
_kproftimer(uintptr_t pc)
{
	if(kprof.time == 0)
		return;
	/*
	 *  if the pc is coming out of spllo or splx,
	 *  use the pc saved when we went splhi.
	 */
	if(pc>=PTR2UINT(spllo) && pc<=PTR2UINT(spldone))
		pc = m->splpc;

	kprof.buf[0] += TK2MS(1);
	if(kprof.minpc<=pc && pc<kprof.maxpc){
		pc -= kprof.minpc;
		pc >>= LRES;
		kprof.buf[pc] += TK2MS(1);
	}else
		kprof.buf[1] += TK2MS(1);
}

static void
kprofinit(void)
{
	if(SZ != sizeof kprof.buf[0])
		panic("kprof size");
	kproftimer = _kproftimer;
}

static Chan*
kprofattach(Chan *c, Chan *ac, char *spec, int flags)
{
	uint32_t n;

	/* allocate when first used */
	kprof.minpc = KTZERO;
	kprof.maxpc = PTR2UINT(etext);
	kprof.nbuf = (kprof.maxpc-kprof.minpc) >> LRES;
	n = kprof.nbuf*SZ;
	if(kprof.buf == 0) {
		kprof.buf = jehanne_malloc(n);
		if(kprof.buf == 0)
			error(Enomem);
	}
	kproftab[1].length = n;
	return devattach('K', spec);
}

static Walkqid*
kprofwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kproftab, nelem(kproftab), devgen);
}

static long
kprofstat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, kproftab, nelem(kproftab), devgen);
}

static Chan*
kprofopen(Chan *c, unsigned long omode)
{
	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Eperm);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
kprofclose(Chan* _1)
{
}

static long
kprofread(Chan *c, void *va, long n, int64_t off)
{
	uint32_t end;
	uint32_t w, *bp;
	uint8_t *a, *ea;
	uint32_t offset = off;

	switch((int)c->qid.path){
	case Kprofdirqid:
		return devdirread(c, va, n, kproftab, nelem(kproftab), devgen);

	case Kprofdataqid:
		end = kprof.nbuf*SZ;
		if(offset & (SZ-1))
			error(Ebadarg);
		if(offset >= end){
			n = 0;
			break;
		}
		if(offset+n > end)
			n = end-offset;
		n &= ~(SZ-1);
		a = va;
		ea = a + n;
		bp = kprof.buf + offset/SZ;
		while(a < ea){
			w = *bp++;
			*a++ = w>>24;
			*a++ = w>>16;
			*a++ = w>>8;
			*a++ = w>>0;
		}
		break;

	default:
		n = 0;
		break;
	}
	return n;
}

static long
kprofwrite(Chan *c, void *a, long n, int64_t _1)
{
	switch((int)(c->qid.path)){
	case Kprofctlqid:
		if(jehanne_strncmp(a, "startclr", 8) == 0){
			jehanne_memset((char *)kprof.buf, 0, kprof.nbuf*SZ);
			kprof.time = 1;
		}else if(jehanne_strncmp(a, "start", 5) == 0)
			kprof.time = 1;
		else if(jehanne_strncmp(a, "stop", 4) == 0)
			kprof.time = 0;
		break;
	default:
		error(Ebadusefd);
	}
	return n;
}

Dev kprofdevtab = {
	'K',
	"kprof",

	devreset,
	kprofinit,
	devshutdown,
	kprofattach,
	kprofwalk,
	kprofstat,
	kprofopen,
	devcreate,
	kprofclose,
	kprofread,
	devbread,
	kprofwrite,
	devbwrite,
	devremove,
	devwstat,
};
