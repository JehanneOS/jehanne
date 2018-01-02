/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

/*
 *  keyboard scan code input from outside the kernel.
 *  to avoid duplication of keyboard map processing for usb.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

extern	void kbdputsc(int, int);

enum {
	Qdir,
	Qkbd,
};

Dirtab kbintab[] = {
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"kbin",	{Qkbd, 0, QTEXCL},	0,	DMEXCL|0200,
};

static	uint32_t	kbinbusy;	/* test and set whether /dev/kbin is open */

static Chan *
kbinattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach(L'Ι', spec);
}

static Walkqid*
kbinwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kbintab, nelem(kbintab), devgen);
}

static long
kbinstat(Chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, kbintab, nelem(kbintab), devgen);
}

static Chan*
kbinopen(Chan *c, int omode)
{
	c = devopen(c, omode, kbintab, nelem(kbintab), devgen);
	if(c->qid.path == Qkbd &&
	   tas32(&kbinbusy) != 0){
		c->flag &= ~COPEN;
		error(Einuse);
	}
	return c;
}

static void
kbinclose(Chan *c)
{
	if((c->flag & COPEN) == 0)
		return;
	if(c->aux){
		jehanne_free(c->aux);
		c->aux = nil;
	}
	if(c->qid.path == Qkbd)
		kbinbusy = 0;
}

static long
kbinread(Chan *c, void *a, long n, int64_t )
{
	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, kbintab, nelem(kbintab), devgen);
	return 0;
}

static long
kbinwrite(Chan *c, void *a, long n, int64_t)
{
	int i;
	uint8_t *p;

	if(c->qid.type & QTDIR)
		error(Eisdir);
	switch((int)c->qid.path){
	case Qkbd:
		p = a;
		for(i = 0; i < n; i++)
			kbdputsc(*p++, 1);	/* external source */
		break;
	default:
		error(Egreg);
	}
	return n;
}

Dev kbindevtab = {
	L'Ι',
	"kbin",

	devreset,
	devinit,
	devshutdown,
	kbinattach,
	kbinwalk,
	kbinstat,
	kbinopen,
	devcreate,
	kbinclose,
	kbinread,
	devbread,
	kbinwrite,
	devbwrite,
	devremove,
	devwstat,
};
