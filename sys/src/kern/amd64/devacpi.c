/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

/*
 * ACPI 5.0 support.  overly ornate.
 * - split table parsing out from file server
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "acpi.h"
#include <aml.h>

enum {
	/* ACPI PM1 control */
	Pscien		= 1<<0,		/* Generate SCI and not SMI */
	Pbmrld		= 1<<1,		/* busmaster → C0 */
	Pgblrls		= 1<<2,		/* global release */

	/* pm1 events */
	Etimer		= 1<<0,
	Ebme		= 1<<4,
	Eglobal		= 1<<5,
	Epowerbtn	= 1<<8,		/* power button pressed */
	Esleepbtn	= 1<<9,
	Ertc		= 1<<10,
	Epciewake	= 1<<14,
	Ewake		= 1<<15,
};

typedef	struct	Aconf	Aconf;
typedef	struct	Gpe	Gpe;

struct Aconf {
	Lock;
	int	init;
	void	(*powerbutton)(void);

	uint32_t	eventopen;
	Queue	*event;
};

struct Gpe {
	uintptr_t	stsio;		/* port used for status */
	int	stsbit;		/* bit number */
	uintptr_t	enio;		/* port used for enable */
	int	enbit;		/* bit number */
	int	nb;		/* event number */
	char*	obj;		/* handler object  */
	int	id;		/* id as supplied by user */
};

enum {
	CMgpe,				/* gpe name id */
	CMpowerbut,
	CMpower,

	Qdir = 0,
	Qctl,
	Qevent,
};

static Cmdtab ctls[] = {
	{CMgpe,		"gpe",		3},
	{CMpowerbut,	"powerbutton", 2},
	{CMpower,	"power", 2},
};

static Dirtab acpidir[]={
	".",		{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"acpictl",	{Qctl},			0,	0666,
	"acpievent",	{Qevent, 0, QTEXCL},	0,	DMEXCL|0440,
};

static	Gpe*	gpes;	/* General purpose events */
static	int	ngpe;
static	Aconf	aconf;

static int
acpigen(Chan *c, char* _1, Dirtab *tab, int ntab, int i, Dir *dp)
{
	Qid qid;

	if(i == DEVDOTDOT){
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, ".", 0, eve, 0555, dp);
		return 1;
	}
	i++; /* skip first element for . itself */
	if(tab==0 || i>=ntab)
		return -1;
	tab += i;
	qid = tab->qid;
	qid.path &= ~Qdir;
	qid.vers = 0;
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

/* ra/rb are int not uintmem because inb/outb are in the i/o address space. */
static uint32_t
getbanked(int ra, int rb, int sz)
{
	uint32_t r;

	r = 0;
	switch(sz){
	case 1:
		if(ra != 0)
			r |= inb(ra);
		if(rb != 0)
			r |= inb(rb);
		break;
	case 2:
		if(ra != 0)
			r |= ins(ra);
		if(rb != 0)
			r |= ins(rb);
		break;
	case 4:
		if(ra != 0)
			r |= inl(ra);
		if(rb != 0)
			r |= inl(rb);
		break;
	default:
		jehanne_print("getbanked: wrong size\n");
	}
	return r;
}

static uint32_t
setbanked(int ra, int rb, int sz, int v)
{
	uint32_t r;

	r = -1;
	switch(sz){
	case 1:
		if(ra != 0)
			outb(ra, v);
		if(rb != 0)
			outb(rb, v);
		break;
	case 2:
		if(ra != 0)
			outs(ra, v);
		if(rb != 0)
			outs(rb, v);
		break;
	case 4:
		if(ra != 0)
			outl(ra, v);
		if(rb != 0)
			outl(rb, v);
		break;
	default:
		jehanne_print("setbanked: wrong size\n");
	}
	return r;
}

/*
 * we must read the register group *as a whole*
 */
static uint32_t
getpm1ctl(void)
{
	return getbanked(fadt.pm1acntblk, fadt.pm1bcntblk, fadt.pm1cntlen);
}

static uint32_t
getpm1sts(void)
{
	return getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen) & 0xffff;
}

static uint32_t
getpm1en(void)
{
	return getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen)>>16;
}

static void
setpm1en(uint32_t v)
{
	uint32_t r;

	r = getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen);
	r &= 0xffff;
	setbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen, r | v<<16);
}

static void
setpm1sts(uint32_t v)
{
	uint32_t r;

	DBG("acpi: setpm1sts %#ux\n", v);

	r = getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen);
	r &= 0xffff0000;
	setbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen, r | v);
}

static int
getgpeen(int n)
{
	return inb(gpes[n].enio) & 1<<gpes[n].enbit;
}

static void
setgpeen(int n, uint32_t v)
{
	int old;

	DBG("acpi: setgpe %d %d\n", n, v);
	old = inb(gpes[n].enio);
	if(v)
		outb(gpes[n].enio, old | 1<<gpes[n].enbit);
	else
		outb(gpes[n].enio, old & ~(1<<gpes[n].enbit));
}

static void
clrgpests(int n)
{
	outb(gpes[n].stsio, 1<<gpes[n].stsbit);
}

static uint32_t
getgpests(int n)
{
	return inb(gpes[n].stsio) & 1<<gpes[n].stsbit;
}

static void
setpm1ctl(uint32_t a, uint32_t b)
{
	setbanked(fadt.pm1acntblk, 0, fadt.pm1cntlen, a);
	setbanked(0, fadt.pm1bcntblk, fadt.pm1cntlen, b);
}

void
acpipoweroff(void)
{
	uint32_t *t;
	enum {
		Go	= 1<<13,
		Sstate	= 1<<10,
	};

	iprint("acpi: power button: power cycle\n");
	/*
	 * bug: we're assuming that this is a fixed function
	 */
	t = acpicfg.sval[5];
	setpm1ctl(t[0]*Sstate | Go, t[1]*Sstate | Go);
}

void
acpipowercycle(void)
{
	exit(0);
}

void
acpipowernop(void)
{
}

struct {
	char	*name;
	void	(*f)(void);
} pwrbuttab[] = {
	"reset",	acpipowercycle,		/* sic */
	"off",	acpipoweroff,
	"nop",	acpipowernop,
};

static void
acpiintr(Ureg* _1, void* _2)
{
	int i;
	uint32_t sts, en;
	Queue *q;

	for(i = 0; i < ngpe; i++)
		if(getgpests(i)){
			iprint("gpe %d on\n", i);
 			en = getgpeen(i);
			setgpeen(i, 0);
			clrgpests(i);
			if(en != 0)
				jehanne_print("acpinitr: calling gpe %d\n", i);
			/* queue gpe.  reenable after done */
		}
	sts = getpm1sts();		/* § 4.8.3.3.1 */
	en = getpm1en();
	iprint("acpinitr: pm1sts %#ux pm1en %#ux\n", sts, en);
	if(sts&en){
		iprint("acpinitr: enabled: %#ux\n", sts&en);
	}
	setpm1sts(sts);
	if(sts&Epowerbtn){
		if((q = aconf.event) == nil ||
		   qiwrite(q, "powerbutton\n", 12) == -1)
			aconf.powerbutton();
	}
}

static void
initgpes(void)
{
	int i, n0, n1;

	n0 = fadt.gpe0blklen/2;
	n1 = fadt.gpe1blklen/2;
	ngpe = n0 + n1;
	gpes = jehanne_mallocz(sizeof(Gpe) * ngpe, 1);
	for(i = 0; i < n0; i++){
		gpes[i].nb = i;
		gpes[i].stsbit = i&7;
		gpes[i].stsio = fadt.gpe0blk + (i>>3);
		gpes[i].enbit = (n0 + i)&7;
		gpes[i].enio = fadt.gpe0blk + ((n0 + i)>>3);
	}
	for(i = 0; i + n0 < ngpe; i++){
		gpes[i + n0].nb = fadt.gp1base + i;
		gpes[i + n0].stsbit = i&7;
		gpes[i + n0].stsio = fadt.gpe1blk + (i>>3);
		gpes[i + n0].enbit = (n1 + i)&7;
		gpes[i + n0].enio = fadt.gpe1blk + ((n1 + i)>>3);
	}
	for(i = 0; i < ngpe; i++){
		setgpeen(i, 0);
		clrgpests(i);
	}
}

static void
acpiioalloc(uint32_t addr, int len, char *name)
{
	char buf[32];

	if(addr != 0){
		jehanne_snprint(buf, sizeof buf, "acpi %s", name);
		ioalloc(addr, len, 0, buf);
	}
}

static void
init(void)
{
	int i;

	aconf.powerbutton = acpipoweroff;

	/* should we use fadt->xpm* and fadt->xgpe* registers for 64 bits? */
	acpiioalloc(fadt.smicmd, 1, "scicmd");
	acpiioalloc(fadt.pm1aevtblk, fadt.pm1evtlen, "pm1aevt");
	acpiioalloc(fadt.pm1bevtblk, fadt.pm1evtlen, "pm1bevt");
	acpiioalloc(fadt.pm1acntblk, fadt.pm1cntlen, "pm1acnt");
	acpiioalloc(fadt.pm1bcntblk, fadt.pm1cntlen, "pm1bcnt");
	acpiioalloc(fadt.pm2cntblk, fadt.pm2cntlen, "pm2cnt");
	acpiioalloc(fadt.pmtmrblk, fadt.pmtmrlen, "pmtmr");
	acpiioalloc(fadt.gpe0blk, fadt.gpe0blklen, "gpe0");
	acpiioalloc(fadt.gpe1blk, fadt.gpe1blklen, "gpe1");

	initgpes();

	/*
	 * This starts ACPI, which requires we handle
	 * power mgmt events ourselves.
	 */
	if(fadt.sciint == 0)
		return;
	if((getpm1ctl() & Pscien) == 0){
		outb(fadt.smicmd, fadt.acpienable);
		for(i = 0;; i++){
			if(i == 10){
				jehanne_print("acpi: failed to enable\n");
				outb(fadt.smicmd, fadt.acpidisable);
				return;
			}
			if(getpm1ctl() & Pscien)
				break;
		}
	}

	if(0){
		jehanne_print("acpi: enable interrupt\n");
		setpm1sts(getpm1sts());
		setpm1en(Epowerbtn);
		intrenable(fadt.sciint, acpiintr, 0, BUSUNKNOWN, "acpi");
	}
}

static Chan*
acpiattach(Chan *c, Chan *ac, char *spec, int flags)
{
	if(fadt.smicmd == 0)
		error("no acpi");
	return devattach(L'α', spec);
}

static Walkqid*
acpiwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, acpidir, nelem(acpidir), acpigen);
}

static long
acpistat(Chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, acpidir, nelem(acpidir), acpigen);
}

static Chan*
acpiopen(Chan *c, unsigned long omode)
{
	c = devopen(c, omode, acpidir, nelem(acpidir), acpigen);
	switch((uint32_t)c->qid.path){
	case Qevent:
		if(tas32(&aconf.eventopen) != 0){
			c->flag &= ~COPEN;
			error(Einuse);
		}
		if(aconf.event == nil){
			aconf.event = qopen(8*1024, Qmsg, 0, 0);
			if(aconf.event == nil){
				c->flag &= ~COPEN;
				error(Enomem);
			}
			qnoblock(aconf.event, 1);
		}else
			qreopen(aconf.event);
		break;
	}
	return c;
}

static void
acpiclose(Chan *c)
{
	switch((uint32_t)c->qid.path){
	case Qevent:
		if(c->flag & COPEN){
			aconf.eventopen = 0;
			qhangup(aconf.event, nil);
		}
		break;
	}
}

static long
acpiread(Chan *c, void *a, long n, int64_t off)
{
	char *s, *p, *e, buf[256];
	int i;
	long q;

	q = c->qid.path;
	switch(q){
	case Qdir:
		return devdirread(c, a, n, acpidir, nelem(acpidir), acpigen);
	case Qctl:
		p = buf;
		e = buf + sizeof buf;
		for(i = 0; i < nelem(pwrbuttab); i++)
			if(pwrbuttab[i].f == aconf.powerbutton)
				break;
		if(i == nelem(pwrbuttab))
			s = "??";
		else
			s = pwrbuttab[i].name;
		p = jehanne_seprint(p, e, "powerbutton %s\n", s);
		p = jehanne_seprint(p, e, "ngpe %d\n", ngpe);
		USED(p);
		return readstr(off, a, n, buf);

	case Qevent:
		return qread(aconf.event, a, n);
	}
	error(Eperm);
	return -1;
}

static long
acpiwrite(Chan *c, void *a, long n, int64_t _1)
{
	uint32_t i;
	Cmdtab *ct;
	Cmdbuf *cb;

	if(c->qid.path != Qctl)
		error(Eperm);

	cb = parsecmd(a, n);
	if(waserror()){
		jehanne_free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, ctls, nelem(ctls));
	switch(ct->index){
	case CMgpe:
		i = jehanne_strtoul(cb->f[1], nil, 0);
		if(i >= ngpe)
			error("gpe out of range");
		kstrdup(&gpes[i].obj, cb->f[2]);
		DBG("gpe %d %s\n", i, gpes[i].obj);
		setgpeen(i, 1);
		break;
	case CMpowerbut:
		for(i = 0; i < nelem(pwrbuttab); i++)
			if(jehanne_strcmp(cb->f[1], pwrbuttab[i].name) == 0){
				ilock(&aconf);
				aconf.powerbutton = pwrbuttab[i].f;
				iunlock(&aconf);
				break;
			}
		if(i == nelem(pwrbuttab))
			error("unknown power button action");
		break;
	case CMpower:
		if(jehanne_strcmp(cb->f[1], "off") == 0)
			aconf.powerbutton();
		else
			error("unknown power button command");
		break;
	}
	poperror();
	jehanne_free(cb);
	return n;
}

Dev acpidevtab = {
	L'α',
	"acpi",

	devreset,
	init,
	devshutdown,
	acpiattach,
	acpiwalk,
	acpistat,
	acpiopen,
	devcreate,
	acpiclose,
	acpiread,
	devbread,
	acpiwrite,
	devbwrite,
	devremove,
	devwstat,
};
