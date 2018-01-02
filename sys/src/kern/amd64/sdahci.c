/*
 * intel/amd ahci sata controller
 * copyright © 2007-12 coraid, inc.
 */
/* Portions of this file are Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/sd.h"
#include "fis.h"
#include "../port/sdfis.h"
#include "ahci.h"
#include "../port/led.h"

#pragma	varargck	type	"T"	int
#define	dprint(...)	if(debug)	print(__VA_ARGS__); else USED(debug)
#define	idprint(...)	if(prid)	print(__VA_ARGS__); else USED(prid)
#define	aprint(...)	if(datapi)	print(__VA_ARGS__); else USED(datapi)
#define	ledprint(...)	if(dled)	print(__VA_ARGS__); else USED(dled)
#define	Pciwaddrh(a)	0
#define Tname(c)	tname[(c)->type]
#define	Ticks		sys->ticks
#define	MS2TK(t)	(((uint32_t)(t)*HZ)/1000)

enum {
	NCtlr		= 4,
	NCtlrdrv	= 32,
	NDrive		= NCtlr*NCtlrdrv,

	Fahdrs		= 4,

	Read		= 0,
	Write,

	Eesb	= 1<<0,	/* must have (Eesb & Emtype) == 0 */
};

/* pci space configuration */
enum {
	Pmap	= 0x90,
	Ppcs	= 0x91,
	Prev	= 0xa8,
};

enum {
	Tesb,
	Tich,
	Tsb600,
	Tjmicron,
	Tahci,
};

static char *tname[] = {
	"63xxesb",
	"ich",
	"sb600",
	"jmicron",
	"ahci",
};

enum {
	Dnull,
	Dmissing,
	Dnew,
	Dready,
	Derror,
	Dreset,
	Doffline,
	Dportreset,
	Dlast,
};

static char *diskstates[Dlast] = {
	"null",
	"missing",
	"new",
	"ready",
	"error",
	"reset",
	"offline",
	"portreset",
};

extern SDifc sdiahciifc;
typedef struct Ctlr Ctlr;

enum {
	DMautoneg,
	DMsatai,
	DMsataii,
	DMsataiii,
	DMlast,
};

static char *modes[DMlast] = {
	"auto",
	"satai",
	"sataii",
	"sataiii",
};

typedef struct Htab Htab;
struct Htab {
	uint32_t	bit;
	char	*name;
};

typedef struct {
	Lock;

	Ctlr	*ctlr;
	SDunit	*unit;
	char	name[10];
	Aport	*port;
	Aportm	portm;
	Aportc	portc;	/* redundant ptr to port and portm. */
	Ledport;

	uint8_t	drivechange;
	uint8_t	nodma;
	uint8_t	state;

	uint64_t	sectors;
	uint32_t	secsize;
	uint32_t	totick;
	uint32_t	lastseen;
	uint32_t	wait;
	uint8_t	mode;
	uint8_t	active;

	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	uint64_t	wwn;

	uint16_t	info[0x200];

	/*
	 * ahci allows non-sequential ports.
	 * to avoid this hassle, we let
	 * driveno	ctlr*NCtlrdrv + unit
	 * portno	nth available port
	 */
	uint32_t	driveno;
	uint32_t	portno;
} Drive;

struct Ctlr {
	Lock;

	int	type;
	int	enabled;
	SDev	*sdev;
	Pcidev	*pci;

	uint8_t	*mmio;
	uint32_t	*lmmio;
	Ahba	*hba;
	Aenc;
	uint32_t	enctype;

	Drive	rawdrive[NCtlrdrv];
	Drive*	drive[NCtlrdrv];
	int	ndrive;

	uint32_t	missirq;
};

static	Ctlr	iactlr[NCtlr];
static	SDev	sdevs[NCtlr];
static	int	niactlr;

static	Drive	*iadrive[NDrive];
static	int	niadrive;

static	int	debug;
static	int	prid = 1;
static	int	datapi;
static	int	dled;

static char stab[] = {
[0]	'i', 'm',
[8]	't', 'c', 'p', 'e',
[16]	'N', 'I', 'W', 'B', 'D', 'C', 'H', 'S', 'T', 'F', 'X'
};

static void
serrstr(uint32_t r, char *s, char *e)
{
	int i;

	e -= 3;
	for(i = 0; i < nelem(stab) && s < e; i++)
		if(r & (1<<i) && stab[i]){
			*s++ = stab[i];
			if(SerrBad & (1<<i))
				*s++ = '*';
		}
	*s = 0;
}

static char ntab[] = "0123456789abcdef";

static void
preg(uint8_t *reg, int n)
{
	char buf[25*3+1], *e;
	int i;

	e = buf;
	for(i = 0; i < n; i++){
		*e++ = ntab[reg[i] >> 4];
		*e++ = ntab[reg[i] & 0xf];
		*e++ = ' ';
	}
	*e++ = '\n';
	*e = 0;
	dprint(buf);
}

static void
dreg(char *s, Aport *p)
{
	dprint("%stask=%lux; cmd=%lux; ci=%lux; is=%lux\n",
		s, p->task, p->cmd, p->ci, p->isr);
}

static void
esleep(int ms)
{
	if(waserror())
		return;
	tsleep(&up->sleep, return0, 0, ms);
	poperror();
}

typedef struct {
	Aport	*p;
	int	i;
} Asleep;

static int
ahciclear(void *v)
{
	Asleep *s;

	s = v;
	return (s->p->ci & s->i) == 0;
}

static void
aesleep(Aportm *m, Asleep *a, int ms)
{
	if(waserror())
		return;
	tsleep(m, ahciclear, a, ms);
	poperror();
}

static int
ahciwait(Aportc *c, int ms)
{
	Aport *p;
	Asleep as;

	p = c->p;
	p->ci = 1;
	as.p = p;
	as.i = 1;
	aesleep(c->m, &as, ms);
	if((p->task & 1) == 0 && p->ci == 0)
		return 0;
	dreg("ahciwait fail/timeout ", c->p);
	return -1;
}

static Alist*
mkalist(Aportm *m, uint32_t flags, uint8_t *data, int len)
{
	Actab *t;
	Alist *l;
	Aprdt *p;

	t = m->ctab;
	if(data && len > 0){
		p = &t->prdt;
		p->dba = PCIWADDR(data);
		p->dbahi = Pciwaddrh(data);
		p->count = 1<<31 | len - 2 | 1;
		flags |= 1<<16;
	}
	l = m->list;
	l->flags = flags | 0x5;
	l->len = 0;
	l->ctab = PCIWADDR(t);
	l->ctabhi = Pciwaddrh(t);
	return l;
}

static int
nop(Aportc *pc)
{
	uint8_t *c;

	c = pc->m->ctab->cfis;
	if(txmodefis(pc->m, c, f) == -1)
		return 0;
	mkalist(pc->m, Lwrite, 0, 0);
	return ahciwait(pc, 3*1000);
}

static void
asleep(int ms)
{
	if(up == nil || !islo())
		delay(ms);
	else
		esleep(ms);
}

static int
ahciportreset(Aportc *c, uint32_t mode)
{
	uint32_t *cmd, i;
	Aport *p;

	p = c->p;
	cmd = &p->cmd;
	*cmd &= ~(Afre|Ast);
	for(i = 0; i < 500; i += 25){
		if((*cmd & Acr) == 0)
			break;
		asleep(25);
	}
	if((*cmd & Apwr) != Apwr)
		*cmd |= Apwr;
	p->sctl = 3*Aipm | 0*Aspd | Adet;
	delay(1);
	p->sctl = 3*Aipm | mode*Aspd;
	return 0;
}

static int
ahciflushcache(Aportc *pc)
{
	uint8_t *c;

	c = pc->m->ctab->cfis;
	flushcachefis(pc->m, c);
	mkalist(pc->m, Lwrite, 0, 0);

	if(ahciwait(pc, 60000) == -1 || pc->p->task & (1|32)){
		dprint("ahciflushcache fail [task %lux]\n", pc->p->task);
//		preg(pc->m->fis.r, 20);
		return -1;
	}
	return 0;
}

static int
ahciidentify0(Aportc *pc, void *id)
{
	uint8_t *c;
	Actab *t;

	t = pc->m->ctab;
	c = t->cfis;
	memset(id, 0, 0x200);
	identifyfis(pc->m, c);
	mkalist(pc->m, 0, id, 0x200);
	return ahciwait(pc, 3*1000);
}

static vlong
ahciidentify(Aportc *pc, uint16_t *id, uint32_t *ss, char *d)
{
	int i, n;
	vlong s;
	Aportm *m;

	m = pc->m;
	for(i = 0;; i++){
		if(i > 5 || ahciidentify0(pc, id) != 0)
			return -1;
		n = idpuis(id);
		if(n & Pspinup && setfeatures(pc, 7, 20*1000) == -1)
			print("%s: puis spinup fail\n", d);
		if(n & Pidready)
			break;
		print("%s: puis waiting\n", d);
	}
	s = idfeat(m, id);
	*ss = idss(m, id);
	if(s == -1 || (m->feat&Dlba) == 0){
		if((m->feat&Dlba) == 0)
			dprint("%s: no lba support\n", d);
		return -1;
	}
	return s;
}

static int
ahciquiet(Aport *a)
{
	uint32_t *p, i;

	p = &a->cmd;
	*p &= ~Ast;
	for(i = 0; i < 500; i += 50){
		if((*p & Acr) == 0)
			goto stop;
		asleep(50);
	}
	return -1;
stop:
	if((a->task & (ASdrq|ASbsy)) == 0){
		*p |= Ast;
		return 0;
	}

	*p |= Aclo;
	for(i = 0; i < 500; i += 50){
		if((*p & Aclo) == 0)
			goto stop1;
		asleep(50);
	}
	return -1;
stop1:
	/* extra check */
	dprint("ahci: clo clear [task %lux]\n", a->task);
	if(a->task & ASbsy)
		return -1;
	*p |= Afre | Ast;
	return 0;
}

static int
ahcicomreset(Aportc *pc)
{
	uint8_t *c;

	dreg("comreset ", pc->p);
	if(ahciquiet(pc->p) == -1){
		dprint("ahci: ahciquiet failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);

	c = pc->m->ctab->cfis;
	nopfis(pc->m, c, 1);
	mkalist(pc->m, Lclear | Lreset, 0, 0);
	if(ahciwait(pc, 500) == -1){
		dprint("ahci: comreset1 failed\n");
		return -1;
	}
	microdelay(250);
	dreg("comreset ", pc->p);

	nopfis(pc->m, c, 0);
	mkalist(pc->m, Lwrite, 0, 0);
	if(ahciwait(pc, 150) == -1){
		dprint("ahci: comreset2 failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);
	return 0;
}

static int
ahciidle(Aport *port)
{
	uint32_t *p, i, r;

	p = &port->cmd;
	if((*p & Arun) == 0)
		return 0;
	*p &= ~Ast;
	r = 0;
	for(i = 0; i < 500; i += 25){
		if((*p & Acr) == 0)
			goto stop;
		asleep(25);
	}
	r = -1;
stop:
	if((*p & Afre) == 0)
		return r;
	*p &= ~Afre;
	for(i = 0; i < 500; i += 25){
		if((*p & Afre) == 0)
			return 0;
		asleep(25);
	}
	return -1;
}

/*
 * §6.2.2.1  first part; comreset handled by reset disk.
 *	- remainder is handled by configdisk.
 *	- ahcirecover is a quick recovery from a failed command.
 */
static int
ahciswreset(Aportc *pc)
{
	int i;

	i = ahciidle(pc->p);
	pc->p->cmd |= Afre;
	if(i == -1)
		return -1;
	if(pc->p->task & (ASdrq|ASbsy))
		return -1;
	return 0;
}

static int
ahcirecover(Aportc *pc)
{
	ahciswreset(pc);
	pc->p->cmd |= Ast;
	if(settxmode(pc, pc->m->udma) == -1)
		return -1;
	return 0;
}

static void*
malign(int size, int align)
{
	void *v;

	v = xspanalloc(size, align, 0);
	memset(v, 0, size);
	return v;
}

static void
setupfis(Afis *f)
{
	f->base = malign(0x100, 0x100);
	f->d = f->base + 0;
	f->p = f->base + 0x20;
	f->r = f->base + 0x40;
	f->u = f->base + 0x60;
	f->devicebits = (uint32_t*)(f->base + 0x58);
}

static void
ahciwakeup(Aportc *c, uint32_t mode)
{
	uint16_t s;
	Aport *p;

	p = c->p;
	s = p->sstatus;
	if((s & Isleepy) == 0)
		return;
	if((s & Smask) != Spresent){
		dprint("ahci: slumbering drive missing [ss %.3ux]\n", s);
		return;
	}
	ahciportreset(c, mode);
	dprint("ahci: wake %.3ux -> %.3lux\n", s, c->p->sstatus);
}

static int
ahciconfigdrive(Ahba *h, Aportc *c, int mode)
{
	Aportm *m;
	Aport *p;
	int i;

	p = c->p;
	m = c->m;

	if(m->list == 0){
		setupfis(&m->fis);
		m->list = malign(sizeof *m->list, 1024);
		m->ctab = malign(sizeof *m->ctab, 128);
	}

	if(ahciidle(p) == -1){
		dprint("ahci: port not idle\n");
		return -1;
	}

	p->list = PCIWADDR(m->list);
	p->listhi = Pciwaddrh(m->list);
	p->fis = PCIWADDR(m->fis.base);
	p->fishi = Pciwaddrh(m->fis.base);

	p->cmd |= Afre;

	if((p->cmd & Apwr) != Apwr)
		p->cmd |= Apwr;

	if((h->cap & Hss) != 0){
		dprint("ahci: spin up ... [%.3lux]\n", p->sstatus);
		for(i = 0; i < 1400; i += 50){
			if((p->sstatus & Sbist) != 0)
				break;
			if((p->sstatus & Smask) == Sphylink)
				break;
			asleep(50);
		}
	}

	if((p->sstatus & SSmask) == (Isleepy | Spresent))
		ahciwakeup(c, mode);

	p->serror = SerrAll;
	p->ie = IEM;

	/* we will get called again once phylink has been established */
	if((p->sstatus & Smask) != Sphylink)
		return 0;

	/* disable power managment sequence from book. */
	p->sctl = 3*Aipm | mode*Aspd | 0*Adet;
	p->cmd &= ~Aalpe;

	p->cmd |= Afre | Ast;

	return 0;
}

static int
ahcienable(Ahba *h)
{
	h->ghc |= Hie;
	return 0;
}

static int
ahcidisable(Ahba *h)
{
	h->ghc &= ~Hie;
	return 0;
}

static int
countbits(uint32_t u)
{
	int i, n;

	n = 0;
	for(i = 0; i < 32; i++)
		if(u & (1<<i))
			n++;
	return n;
}

static int
ahciconf(Ctlr *c)
{
	uint32_t u;
	Ahba *h;

	h = c->hba = (Ahba*)c->mmio;
	u = h->cap;

	if((u & Ham) == 0)
		h->ghc |= Hae;

	return countbits(h->pi);
}

static int
ahcihandoff(Ahba *h)
{
	int wait;

	if((h->cap2 & Boh) == 0)
		return 0;
	h->bios |= Oos;
	for(wait = 0; wait < 2000; wait += 100){
		if((h->bios & Bos) == 0)
			return 0;
		delay(100);
	}
	iprint("ahci: bios handoff timed out\n");
	return -1;
}

static int
ahcihbareset(Ahba *h)
{
	int wait;

	h->ghc |= Hhr;
	for(wait = 0; wait < 1000; wait += 100){
		if((h->ghc & Hhr) == 0)
			return 0;
		delay(100);
	}
	return -1;
}

static char*
dnam(Drive *d)
{
	char *s;

	s = d->name;
	if(d->unit && d->unit->name)
		s = d->unit->name;
	return s;
}

static int
identify(Drive *d)
{
	uint8_t oserial[21];
	uint16_t *id;
	vlong osectors, s;
	SDunit *u;

	id = d->info;
	s = ahciidentify(&d->portc, id, &d->secsize, dnam(d));
	if(s == -1)
		return -1;
	osectors = d->sectors;
	memmove(oserial, d->serial, sizeof d->serial);

	d->sectors = s;

	idmove(d->serial, id+10, 20);
	idmove(d->firmware, id+23, 8);
	idmove(d->model, id+27, 40);
	d->wwn = idwwn(d->portc.m, id);

	u = d->unit;
	memset(u->inquiry, 0, sizeof u->inquiry);
	u->inquiry[2] = 2;
	u->inquiry[3] = 2;
	u->inquiry[4] = sizeof u->inquiry - 4;
	memmove(u->inquiry+8, d->model, 40);

	if(osectors != s || memcmp(oserial, d->serial, sizeof oserial)){
		d->drivechange = 1;
		d->nodma = 0;
		u->sectors = 0;
	}
	return 0;
}

static void
clearci(Aport *p)
{
	if(p->cmd & Ast){
		p->cmd &= ~Ast;
		p->cmd |=  Ast;
	}
}

static int
intel(Ctlr *c)
{
	return c->pci->vid == 0x8086;
}

static int
ignoreahdrs(Drive *d)
{
	return d->portm.feat & Datapi && d->ctlr->type == Tsb600;
}

static void
updatedrive(Drive *d)
{
	uint32_t f, cause, serr, s0, pr, ewake;
	Aport *p;
	static uint32_t last;

	pr = 1;
	ewake = 0;
	f = 0;
	p = d->port;
	cause = p->isr;
	if(d->ctlr->type == Tjmicron)
		cause &= ~Aifs;
	serr = p->serror;
	p->isr = cause;

	if(p->ci == 0){
		f |= Fdone;
		pr = 0;
	}else if(cause & Adps){
		pr = 0;
	}else if(cause & Atfes){
		f |= Ferror;
		ewake = 1;
		pr = 0;
	}
	if(cause & Ifatal){
		ewake = 1;
		dprint("%s: fatal\n", dnam(d));
	}
	if(cause & Adhrs){
		if(p->task & 33){
			if(ignoreahdrs(d) && serr & ErrE)
				f |= Fahdrs;
			dprint("%s: Adhrs cause %lux serr %lux task %lux\n",
				dnam(d), cause, serr, p->task);
			f |= Ferror;
			ewake = 1;
		}
		pr = 0;
	}
	if(p->task & 1 && last != cause)
		dprint("%s: err ca %lux serr %lux task %lux sstat %.3lux\n",
			dnam(d), cause, serr, p->task, p->sstatus);
	if(pr)
		dprint("%s: upd %lux ta %lux\n", dnam(d), cause, p->task);

	if(cause & (Aprcs|Aifs)){
		s0 = d->state;
		switch(p->sstatus & Smask){
		case Smissing:
			d->state = Dmissing;
			break;
		case Spresent:
			if((p->sstatus & Imask) == Islumber)
				d->state = Dnew;
			else
				d->state = Derror;
			break;
		case Sphylink:
			/* power mgnt crap for suprise removal */
			p->ie |= Aprcs|Apcs;	/* is this required? */
			d->state = Dreset;
			break;
		case Sbist:
			d->state = Doffline;
			break;
		}
		dprint("%s: updatedrive: %s → %s [ss %.3lux]\n",
			dnam(d), diskstates[s0], diskstates[d->state], p->sstatus);
		if(s0 == Dready && d->state != Dready)
			dprint("%s: pulled\n", dnam(d));
		if(d->state != Dready)
			f |= Ferror;
		if(d->state != Dready || p->ci)
			ewake = 1;
	}
	p->serror = serr;
	if(ewake)
		clearci(p);
	if(f){
		d->portm.flag = f;
		wakeup(&d->portm);
	}
	last = cause;
}

static void
dstatus(Drive *d, int s)
{
	dprint("%s: dstatus: %s → %s from pc=%p\n", dnam(d), 
		diskstates[d->state], diskstates[s], getcallerpc(&d));

	ilock(d);
	d->state = s;
	iunlock(d);
}

static void
configdrive(Drive *d)
{
	if(ahciconfigdrive(d->ctlr->hba, &d->portc, d->mode) == -1){
		dstatus(d, Dportreset);
		return;
	}

	ilock(d);
	switch(d->port->sstatus & Smask){
	default:
	case Smissing:
		d->state = Dmissing;
		break;
	case Spresent:
		if(d->state == Dnull)
			d->state = Dportreset;
		break;
	case Sphylink:
		if(d->state == Dready)
			break;
		d->wait = 0;
		d->state = Dnew;
		break;
	case Sbist:
		d->state = Doffline;
		break;
	}
	iunlock(d);

	dprint("%s: configdrive: %s\n", dnam(d), diskstates[d->state]);
}

static void
resetdisk(Drive *d)
{
	uint32_t state, det, stat;
	Aport *p;

	p = d->port;
	det = p->sctl & 7;
	stat = p->sstatus & Smask;
	state = (p->cmd>>28) & 0xf;
	dprint("%s: resetdisk [icc %ux; det %.3ux; sdet %.3ux]\n", dnam(d), state, det, stat);

	ilock(d);
	if(d->state != Dready && d->state != Dnew)
		d->portm.flag |= Ferror;
	if(stat != Sphylink)
		d->state = Dportreset;
	else
		d->state = Dreset;
	clearci(p);			/* satisfy sleep condition. */
	wakeup(&d->portm);
	iunlock(d);

	if(stat != Sphylink)
		return;

	qlock(&d->portm);
	if(p->cmd&Ast && ahciswreset(&d->portc) == -1)
		dstatus(d, Dportreset);	/* get a bigger stick. */
	else
		configdrive(d);
	qunlock(&d->portm);
}

static int
newdrive(Drive *d)
{
	char *s;
	Aportc *c;
	Aportm *m;

	c = &d->portc;
	m = &d->portm;

	qlock(c->m);
	setfissig(m, c->p->sig);
	if(identify(d) == -1){
		dprint("%s: identify failure\n", dnam(d));
		goto lose;
	}
	if(settxmode(c, m->udma) == -1){
		dprint("%s: can't set udma mode\n", dnam(d));
		goto lose;
	}
	if(m->feat & Dpower && setfeatures(c, 0x85, 3*1000) == -1){
		dprint("%s: can't disable apm\n", dnam(d));
		m->feat &= ~Dpower;
		if(ahcirecover(c) == -1)
			goto lose;
	}
	dstatus(d, Dready);
	qunlock(c->m);

	s = "";
	if(m->feat & Dllba)
		s = "L";
	idprint("%s: %sLBA %,lld sectors\n", dnam(d), s, d->sectors);
	idprint("  %s %s %s %s\n", d->model, d->firmware, d->serial,
		d->drivechange? "[newdrive]": "");
	return 0;

lose:
	idprint("%s: can't be initialized\n", dnam(d));
	dstatus(d, Dnull);
	qunlock(c->m);
	return -1;
}

enum {
	Nms		= 256,
	Mphywait	=  2*1024/Nms - 1,
	Midwait		= 16*1024/Nms - 1,
	Mcomrwait	= 64*1024/Nms - 1,
};

static void
hangck(Drive *d)
{
	if(d->active && d->totick != 0 && (long)(Ticks - d->totick) > 0){
		dprint("%s: drive hung [task %lux; ci %lux; serr %lux]%s\n",
			dnam(d), d->port->task, d->port->ci, d->port->serror,
			d->nodma == 0 ? "; disabling dma" : "");
		d->nodma = 1;
		d->state = Dreset;
	}
}

static uint16_t olds[NCtlr*NCtlrdrv];

static void
doportreset(Drive *d)
{
	qlock(&d->portm);
	ahciportreset(&d->portc, d->mode);
	qunlock(&d->portm);

	dprint("ahci: portreset: %s [task %lux; ss %.3lux]\n",
		diskstates[d->state], d->port->task, d->port->sstatus);
}

/* drive must be locked */
static void
statechange(Drive *d)
{
	switch(d->state){
	case Dnull:
	case Doffline:
		if(d->unit)
		if(d->unit->sectors != 0){
			d->sectors = 0;
			d->drivechange = 1;
		}
	case Dready:
		d->wait = 0;
	}
}

static uint32_t
maxmode(Ctlr *c)
{
	return (c->hba->cap & 0xf*Hiss)/Hiss;
}

static void iainterrupt(Ureg*, void *);

static void
checkdrive(Drive *d, int i)
{
	uint16_t s, sig;

	if(d->ctlr->enabled == 0)
		return;
	if(d->driveno == 0)
		iainterrupt(0, d->ctlr);	/* check for missed irq's */

	ilock(d);
	s = d->port->sstatus;
	if(s)
		d->lastseen = Ticks;
	if(s != olds[i]){
		dprint("%s: status: %.3ux -> %.3ux: %s\n",
			dnam(d), olds[i], s, diskstates[d->state]);
		olds[i] = s;
		d->wait = 0;
	}
	hangck(d);
	switch(d->state){
	case Dnull:
	case Dready:
		break;
	case Dmissing:
	case Dnew:
		switch(s & (Iactive|Smask)){
		case Spresent:
			ahciwakeup(&d->portc, d->mode);
		case Smissing:
			break;
		default:
			dprint("%s: unknown status %.3ux\n", dnam(d), s);
			/* fall through */
		case Iactive:		/* active, no device */
			if(++d->wait&Mphywait)
				break;
reset:
			if(d->mode == 0)
				d->mode = maxmode(d->ctlr);
			else
				d->mode--;
			if(d->mode == DMautoneg){
				d->state = Dportreset;
				goto portreset;
			}
			dprint("%s: reset; new mode %s\n", dnam(d),
				modes[d->mode]);
			iunlock(d);
			resetdisk(d);
			ilock(d);
			break;
		case Iactive | Sphylink:
			if(d->unit == nil)
				break;
			if((++d->wait&Midwait) == 0){
				dprint("%s: slow reset [task %lux; ss %.3ux; wait %d]\n",
					dnam(d), d->port->task, s, d->wait);
				goto reset;
			}
			s = (uint8_t)d->port->task;
			sig = d->port->sig >> 16;
			if(s == 0x7f || s&ASbsy ||
			    (sig != 0xeb14 && (s & ASdrdy) == 0))
				break;
			iunlock(d);
			newdrive(d);
			ilock(d);
			break;
		}
		break;
	case Doffline:
		if(d->wait++ & Mcomrwait)
			break;
		/* fallthrough */
	case Derror:
	case Dreset:
		dprint("%s: reset [%s]: mode %d; status %.3ux\n",
			dnam(d), diskstates[d->state], d->mode, s);
		iunlock(d);
		resetdisk(d);
		ilock(d);
		break;
	case Dportreset:
portreset:
		if(d->wait++ & 0xff && (s & Iactive) == 0)
			break;
		dprint("%s: portreset [%s]: mode %d; status %.3ux\n",
			dnam(d), diskstates[d->state], d->mode, s);
		d->portm.flag |= Ferror;
		clearci(d->port);
		wakeup(&d->portm);
		if((s & Smask) == Smissing){
			d->state = Dmissing;
			break;
		}
		iunlock(d);
		doportreset(d);
		ilock(d);
		break;
	}
	statechange(d);
	iunlock(d);
}

static void
satakproc(void*)
{
	int i;

	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, 0, Nms);
		for(i = 0; i < niadrive; i++)
			checkdrive(iadrive[i], i);
	}
}

static void
iainterrupt(Ureg *u, void *a)
{
	int i;
	uint32_t cause, m;
	Ctlr *c;
	Drive *d;

	c = a;
	ilock(c);
	cause = c->hba->isr;
	for(i = 0; cause; i++){
		m = 1 << i;
		if((cause & m) == 0)
			continue;
		cause &= ~m;
		d = c->rawdrive + i;
		ilock(d);
		if(d->port != nil && d->port->isr && c->hba->pi & m)
			updatedrive(d);
		c->hba->isr = m;
		iunlock(d);
	}
	if(u == 0 && i > 0)
		c->missirq++;
	iunlock(c);
}

static int
ahciencreset(Ctlr *c)
{
	Ahba *h;

	if(c->enctype == Eesb)
		return 0;
	h = c->hba;
	h->emctl |= Emrst;
	while(h->emctl & Emrst)
		delay(1);
	return 0;
}

/*
 * from the standard: (http://en.wikipedia.org/wiki/IBPI)
 * rebuild is preferred as locate+fail; alternate 1hz fail
 * we're going to assume no locate led.
 */

enum {
	Ledsleep	= 125,		/* 8hz */

	N0	= Ledon*Aled,
	L0	= Ledon*Aled | Ledon*Locled,
	L1	= Ledon*Aled | Ledoff*Locled,
	R0	= Ledon*Aled | Ledon*Locled |	Ledon*Errled,
	R1	= Ledon*Aled | 			Ledoff*Errled,
	S0	= Ledon*Aled |  Ledon*Locled /*|	Ledon*Errled*/,	/* botch */
	S1	= Ledon*Aled | 			Ledoff*Errled,
	P0	= Ledon*Aled | 			Ledon*Errled,
	P1	= Ledon*Aled | 			Ledoff*Errled,
	F0	= Ledon*Aled | 			Ledon*Errled,
	C0	= Ledon*Aled | Ledon*Locled,
	C1	= Ledon*Aled | Ledoff*Locled,

};

//static uint16_t led3[Ibpilast*8] = {
//[Ibpinone*8]	0,	0,	0,	0,	0,	0,	0,	0,
//[Ibpinormal*8]	N0,	N0,	N0,	N0,	N0,	N0,	N0,	N0,
//[Ibpirebuild*8]	R0,	R0,	R0,	R0,	R1,	R1,	R1,	R1,
//[Ibpilocate*8]	L0,	L1,	L0,	L1,	L0,	L1,	L0,	L1,
//[Ibpispare*8]	S0,	S1,	S0,	S1,	S1,	S1,	S1,	S1,
//[Ibpipfa*8]	P0,	P1,	P0,	P1,	P1,	P1,	P1,	P1,	/* first 1 sec */
//[Ibpifail*8]	F0,	F0,	F0,	F0,	F0,	F0,	F0,	F0,
//[Ibpicritarray*8]	C0,	C0,	C0,	C0,	C1,	C1,	C1,	C1,
//[Ibpifailarray*8]	C0,	C1,	C0,	C1,	C0,	C1,	C0,	C1,
//};

static uint16_t led2[Ibpilast*8] = {
[Ibpinone*8]	0,	0,	0,	0,	0,	0,	0,	0,
[Ibpinormal*8]	N0,	N0,	N0,	N0,	N0,	N0,	N0,	N0,
[Ibpirebuild*8]	R0,	R0,	R0,	R0,	R1,	R1,	R1,	R1,
[Ibpilocate*8]	L0,	L0,	L0,	L0,	L0,	L0,	L0,	L0,
[Ibpispare*8]	S0,	S0,	S0,	S0,	S1,	S1,	S1,	S1,
[Ibpipfa*8]	P0,	P1,	P0,	P1,	P1,	P1,	P1,	P1,	/* first 1 sec */
[Ibpifail*8]	F0,	F0,	F0,	F0,	F0,	F0,	F0,	F0,
[Ibpicritarray*8]	C0,	C0,	C0,	C0,	C1,	C1,	C1,	C1,
[Ibpifailarray*8]	C0,	C1,	C0,	C1,	C0,	C1,	C0,	C1,
};

static int
ledstate(Ledport *p, uint32_t seq)
{
	uint16_t i;

	if(p->led == Ibpipfa && seq%32 >= 8)
		i = P1;
	else
		i = led2[8*p->led + seq%8];
	if(i != p->ledbits){
		p->ledbits = i;
		ledprint("ledstate %,.011ub %ud\n", p->ledbits, seq);
		return 1;
	}
	return 0;
}

static int
blink(Drive *d, uint32_t t)
{
	Ahba *h;
	Ctlr *c;
	Aledmsg msg;

	if(ledstate(d, t) == 0)
		return 0;
	c = d->ctlr;
	h = c->hba;
	/* ensure last message has been transmitted */
	while(h->emctl & Tmsg)
		microdelay(1);
	switch(c->enctype){
	default:
		panic("%s: bad led type %d", dnam(d), c->enctype);
	case Elmt:
		memset(&msg, 0, sizeof msg);
		msg.type = Mled;
		msg.dsize = 0;
		msg.msize = sizeof msg - 4;
		msg.led[0] = d->ledbits;
		msg.led[1] = d->ledbits>>8;
		msg.pm = 0;
		msg.hba = d->driveno;
		memmove(c->enctx, &msg, sizeof msg);
		break;
	}
	h->emctl |= Tmsg;
	return 1;
}

enum {
	Esbdrv0	= 4,		/* start pos in bits */
	Esbiota	= 3,		/* shift in bits */
	Esbact	= 1,
	Esbloc	= 2,
	Esberr	= 4,
};

uint32_t
esbbits(uint32_t s)
{
	uint32_t i, e;				/* except after c */

	e = 0;
	for(i = 0; i < 3; i++)
		e |= ((s>>3*i & 7) != 0)<<i;
	return e;
}

static int
blinkesb(Ctlr *c, uint32_t t)
{
	uint32_t i, s, u[32/4];
	uint64_t v;
	Drive *d;

	s = 0;
	for(i = 0; i < c->ndrive; i++){
		d = c->drive[i];
		s |= ledstate(d, t);		/* no port mapping */
	}
	if(s == 0)
		return 0;
	memset(u, 0, sizeof u);
	for(i = 0; i < c->ndrive; i++){
		d = c->drive[i];
		s = Esbdrv0 + Esbiota*i;
		v = esbbits(d->ledbits) * (1ull << s%32);
		u[s/32 + 0] |= v;
		u[s/32 + 1] |= v>>32;
	}
	for(i = 0; i < c->encsz; i++)
		c->enctx[i] = u[i];
	return 1;
}

static long
ahciledr(SDunit *u, Chan *ch, void *a, long n, vlong off)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	return ledr(d, ch, a, n, off);
}

static long
ahciledw(SDunit *u, Chan *ch, void *a, long n, vlong off)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	return ledw(d, ch, a, n, off);
}

static void
ledkproc(void*)
{
	uint8_t map[NCtlr];
	uint32_t i, j, t0, t1;
	Ctlr *c;
	Drive *d;

	j = 0;
	memset(map, 0, sizeof map);
	for(i = 0; i < niactlr; i++)
		if(iactlr[i].enctype != 0){
			ahciencreset(iactlr + i);
			map[i] = 1;
			j++;
		}
	if(j == 0)
		pexit("no work", 1);
	for(i = 0; i < niadrive; i++){
		iadrive[i]->nled = 3;		/* hardcoded */
		if(iadrive[i]->ctlr->enctype == Eesb)
			iadrive[i]->nled = 3;
		iadrive[i]->ledbits = -1;
	}
	for(i = 0; ; i++){
		t0 = Ticks;
		for(j = 0; j < niadrive; ){
			c = iadrive[j]->ctlr;
			if(map[j] == 0)
				j += c->enctype;
			else if(c->enctype == Eesb){
				blinkesb(c, i);
				j += c->ndrive;
			}else{
				d = iadrive[j++];
				blink(d, i);
			}
		}
		t1 = Ticks;
		esleep(Ledsleep - TK2MS(t1 - t0));
	}
}

static int
waitready(Drive *d)
{
	uint32_t s, i, δ;

	for(i = 0;; i += 250){
		if(d->state == Dreset || d->state == Dportreset || d->state == Dnew)
			return 1;
		ilock(d);
		s = d->port->sstatus;
		if(d->state == Dready && (s & Smask) == Sphylink){
			iunlock(d);
			return 0;
		}
		δ = Ticks - d->lastseen;
		if(d->state == Dnull || δ > 10*1000)
			break;
		if((s & Imask) == 0 && δ > 1500)
			break;
		if(i >= 15*1000){
			d->state = Doffline;
			iunlock(d);
			print("%s: not responding; offline\n", dnam(d));
			return -1;
		}
		iunlock(d);
		esleep(250);
	}
	iunlock(d);
	return -1;
}

static int
iaverify(SDunit *u)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	ilock(c);
	ilock(d);
	if(d->unit == nil){
		d->unit = u;
		if(c->enctype != 0)
			sdaddfile(u, "led", 0644, eve, ahciledr, ahciledw);
	}
	iunlock(d);
	iunlock(c);
	checkdrive(d, d->driveno);		/* c->d0 + d->driveno */
	return 1;
}

static int
iaonline(SDunit *u)
{
	int r;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];

	while(d->state != Dmissing && waitready(d) == 1)
		esleep(1);

	dprint("%s: iaonline: %s\n", dnam(d), diskstates[d->state]);

	ilock(d);
	if(d->portm.feat & Datapi){
		r = d->drivechange;
		d->drivechange = 0;
		iunlock(d);
		if(r != 0)
			scsiverify(u);
		return scsionline(u);
	}
	r = 0;
	if(d->drivechange){
		d->drivechange = 0;
		r = 2;
	}else if(d->state == Dready)
		r = 1;
	if(r){
		u->sectors = d->sectors;
		u->secsize = d->secsize;
	}
	iunlock(d);

	return r;
}

static int
iaenable(SDev *s)
{
	char name[32];
	Ctlr *c;
	static int once;

	c = s->ctlr;
	ilock(c);
	if(!c->enabled){
		if(once == 0)
			kproc("iasata", satakproc, 0);
		if(c->ndrive == 0)
			panic("iaenable: zero s->ctlr->ndrive");
		pcisetbme(c->pci);
		snprint(name, sizeof name, "%s (%s)", s->name, s->ifc->name);
		intrenable(c->pci->intl, iainterrupt, c, c->pci->tbdf, name);
		/* supposed to squelch leftover interrupts here. */
		ahcienable(c->hba);
		c->enabled = 1;
		if(++once == niactlr)
			kproc("ialed", ledkproc, 0);
	}
	iunlock(c);
	return 1;
}

static int
iadisable(SDev *s)
{
	char name[32];
	Ctlr *c;

	c = s->ctlr;
	ilock(c);
	ahcidisable(c->hba);
	snprint(name, sizeof name, "%s (%s)", s->name, s->ifc->name);
	intrdisable(c->pci->intl, iainterrupt, c, c->pci->tbdf, name);
	c->enabled = 0;
	iunlock(c);
	return 1;
}

static Alist*
ahcibuild(Drive *d, int rw, void *data, int nsect, vlong lba)
{
	uint8_t *c;
	uint32_t flags;
	Aportm *m;

	m = &d->portm;
	c = m->ctab->cfis;
	rwfis(m, c, rw, nsect, lba);
	flags = Lpref;
	if(rw == SDwrite)
		flags |= Lwrite;
	return mkalist(m, flags, data, nsect * d->secsize);
}

static Alist*
ahcibuildpkt(Drive *d, SDreq *r, void *data, int n)
{
	uint32_t flags;
	Aportm *m;
	uint8_t *c;
	Actab *t;

	m = &d->portm;
	t = m->ctab;
	c = t->cfis;

	atapirwfis(m, c, r->cmd, r->clen, 0x2000);
	if((n & 15) != 0 || d->nodma)
		c[Ffeat] &= ~1;	/* use pio */
	else if(c[Ffeat] & 1 && d->info[62] & (1<<15))	/* dma direction */
		c[Ffeat] = (c[Ffeat] & ~(1<<2)) | ((r->write == 0) << 2);
	flags = Lpref | Latapi;
	if(r->write != 0 && data)
		flags |= Lwrite;
	return mkalist(m, flags, data, n);
}

static Alist*
ahcibuildfis(Drive *d, SDreq *r, void *data, uint32_t n)
{
	uint32_t flags;
	uint8_t *c;
	Aportm *m;

	if((r->ataproto & Pprotom) == Ppkt)
		return ahcibuildpkt(d, r, data, n);

	m = &d->portm;
	c = m->ctab->cfis;
	memmove(c, r->cmd, r->clen);
	flags = Lpref;
	if(r->write || n == 0)
		flags |= Lwrite;
	return mkalist(m, flags, data, n);
}

static int
lockready(Drive *d)
{
	int i;

	qlock(&d->portm);
	while ((i = waitready(d)) == 1) {
		qunlock(&d->portm);
		esleep(1);
		qlock(&d->portm);
	}
	return i;
}

static int
flushcache(Drive *d)
{
	int i;

	i = -1;
	if(lockready(d) == 0)
		i = ahciflushcache(&d->portc);
	qunlock(&d->portm);
	return i;
}

static int
io(Drive *d, uint32_t proto, int to, int interrupt)
{
	uint32_t task, flag, stat, rv;
	Aport *p;
	Asleep as;

	switch(waitready(d)){
	case -1:
		return SDeio;
	case 1:
		return SDretry;
	}

	ilock(d);
	d->portm.flag = 0;
	iunlock(d);
	p = d->port;
	p->ci = 1;

	as.p = p;
	as.i = 1;
	d->totick = 0;
	if(to > 0)
		d->totick = Ticks + MS2TK(to) | 1;	/* fix fencepost */
	d->active++;

	while(waserror())
		if(interrupt){
			d->active--;
			d->port->ci = 0;
			if(ahcicomreset(&d->portc) == -1)
				dstatus(d, Dreset);
			return SDtimeout;
		}
	
	sleep(&d->portm, ahciclear, &as);
	poperror();

	d->active--;
	ilock(d);
	stat = d->state;
	flag = d->portm.flag;
	task = d->port->task;
	iunlock(d);

	rv = SDok;
	if(proto & Ppkt && stat == Dready){
		rv = task >> 8 + 4 & 0xf;
		flag &= ~Fahdrs;
		flag |= Fdone;
	}else if(task & (Efatal<<8) || task & (ASbsy|ASdrq) && stat == Dready){
		d->port->ci = 0;
		ahcirecover(&d->portc);
		task = d->port->task;
		flag &= ~Fdone;		/* either an error or do-over */
	}
	if(flag == 0){
		print("%s: retry\n", dnam(d));
		return SDretry;
	}
	if(flag & (Fahdrs | Ferror)){
		if((task & Eidnf) == 0)
			print("%s: i/o error %ux\n", dnam(d), task);
		return SDcheck;
	}
	return rv;
}

static int
iariopkt(SDreq *r, Drive *d)
{
	int try, to;
	uint8_t *cmd;
	Alist *l;

	cmd = r->cmd;
	aprint("%s: %.2ux %.2ux %c %d %p\n", dnam(d), cmd[0], cmd[2],
		"rw"[r->write], r->dlen, r->data);

	r->rlen = 0;

	/*
	 * prevent iaonline() to hang forever by timing out
	 * inquiry and capacity commands after 5 seconds.
	 */
	to = 30*1000;
	switch(cmd[0]){
	case 0x9e: if(cmd[1] != 0x10) break;
	case 0x25:
	case 0x12:
		to = 5*1000;
		break;
	}

	for(try = 0; try < 10; try++){
		qlock(&d->portm);
		l = ahcibuildpkt(d, r, r->data, r->dlen);
		r->status = io(d, Ppkt, to, 0);
		switch(r->status){
		case SDeio:
			qunlock(&d->portm);
			return SDeio;
		case SDretry:
			qunlock(&d->portm);
			continue;
		}
		r->rlen = l->len;
		qunlock(&d->portm);
		return SDok;
	}
	print("%s: bad disk\n", dnam(d));
	return r->status = SDcheck;
}

static long
ahcibio(SDunit *u, int lun, int write, void *a, long count, uint64_t lba)
{
	int n, rw, try, status, max;
	uint8_t *data;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	if(d->portm.feat & Datapi)
		return scsibio(u, lun, write, a, count, lba);

	max = 128;
	if(d->portm.feat & Dllba){
		max = 8192;		/* ahci maximum */
		if(c->type == Tsb600)
			max = 255;	/* errata */
	}
	rw = write? SDwrite: SDread;
	data = a;
	dprint("%s: bio: %llud %c %lud %p\n",
		dnam(d), lba, "rw"[rw], count, data);

	for(try = 0; try < 10;){
		n = count;
		if(n > max)
			n = max;
		qlock(&d->portm);
		ahcibuild(d, rw, data, n, lba);
		status = io(d, Pdma, 5000, 0);
		qunlock(&d->portm);
		switch(status){
		case SDeio:
			return -1;
		case SDretry:
			try++;
			continue;
		}
		try = 0;
		count -= n;
		lba += n;
		data += n * u->secsize;
		if(count == 0)
			return data - (uint8_t*)a;
	}
	print("%s: bad disk\n", dnam(d));
	return -1;
}

static int
iario(SDreq *r)
{
	int i, n, count, rw;
	uint8_t *cmd;
	uint64_t lba;
	Ctlr *c;
	Drive *d;
	SDunit *u;

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive[u->subno];
	if(d->portm.feat & Datapi)
		return iariopkt(r, d);
	cmd = r->cmd;

	if(cmd[0] == 0x35 || cmd[0] == 0x91){
		if(flushcache(d) == 0)
			return sdsetsense(r, SDok, 0, 0, 0);
		return sdsetsense(r, SDcheck, 3, 0xc, 2);
	}

	if((i = sdfakescsi(r)) != SDnostatus){
		r->status = i;
		return i;
	}

	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	n = ahcibio(u, r->lun, r->write, r->data, count, lba);
	if(n == -1)
		return SDeio;
	r->rlen = n;
	return SDok;
}

static uint8_t bogusrfis[16] = {
[Ftype]		0x34,
[Fioport]	0x40,
[Fstatus]	0x50,
[Fdev]		0xa0,
};

static void
sdr0(Drive *d)
{
	uint8_t *c;

	c = d->portm.fis.r;
	memmove(c, bogusrfis, sizeof bogusrfis);
	coherence();
}

static int
sdr(SDreq *r, Drive *d, int st)
{
	uint8_t *c;
	uint32_t t;

	if((r->ataproto & Pprotom) == Ppkt){
		t = d->port->task;
		if(t & ASerr)
			st = t >> 8 + 4 & 0xf;
	}
	c = d->portm.fis.r;
	memmove(r->cmd, c, 16);
	r->status = st;
	if(st == SDcheck)
		st = SDok;
	return st;
}

static int
fisreqchk(Sfis *f, SDreq *r)
{
	if((r->ataproto & Pprotom) == Ppkt)
		return SDnostatus;
	/*
	 * handle oob requests;
	 *    restrict & sanitize commands
	 */
	if(r->clen != 16)
		error(Eio);
	if(r->cmd[0] == 0xf0){
		sigtofis(f, r->cmd);
		r->status = SDok;
		return SDok;
	}
	r->cmd[0] = 0x27;
	r->cmd[1] = 0x80;
	r->cmd[7] |= 0xa0;
	return SDnostatus;
}

static int
iaataio(SDreq *r)
{
	int try;
	Ctlr *c;
	Drive *d;
	SDunit *u;
	Alist *l;

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive[u->subno];

	if((r->status = fisreqchk(&d->portm, r)) != SDnostatus)
		return r->status;
	r->rlen = 0;
	sdr0(d);
	for(try = 0; try < 10; try++){
		qlock(&d->portm);
		l = ahcibuildfis(d, r, r->data, r->dlen);
		r->status = io(d, r->ataproto & Pprotom, -1, 1);
		switch(r->status){
		case SDtimeout:
			qunlock(&d->portm);
			return sdsetsense(r, SDcheck, 11, 0, 6);
		case SDeio:
			qunlock(&d->portm);
			return SDeio;
		case SDretry:
			qunlock(&d->portm);
			continue;
		}
		r->rlen = (r->ataproto & Pprotom) == Ppkt ? l->len : r->dlen;
		try = sdr(r, d, r->status);
		qunlock(&d->portm);
		return try;
	}
	print("%s: bad disk\n", dnam(d));
	return r->status = SDeio;
}

/*
 * configure drives 0-5 as ahci sata  (c.f. errata)
 */
static int
iaahcimode(Pcidev *p)
{
	uint32_t u;

	u = pcicfgr16(p, 0x92);
	dprint("ahci: %T: iaahcimode %.2ux %.4ux\n", p->tbdf, pcicfgr8(p, 0x91), u);
	pcicfgw16(p, 0x92, u | 0xf);	/* ports 0-15 */
	return 0;
}

enum{
	Ghc	= 0x04/4,	/* global host control */
	Pi	= 0x0c/4,	/* ports implemented */
	Cmddec	= 1<<15,	/* enable command block decode */

	/* Ghc bits */
	Ahcien	= 1<<31,	/* ahci enable */
};

static void
iasetupahci(Ctlr *c)
{
	pcicfgw16(c->pci, 0x40, pcicfgr16(c->pci, 0x40) & ~Cmddec);
	pcicfgw16(c->pci, 0x42, pcicfgr16(c->pci, 0x42) & ~Cmddec);

	c->lmmio[Ghc] |= Ahcien;
	c->lmmio[Pi] = (1 << 6) - 1;	/* 5 ports (supposedly ro pi reg) */

	/* enable ahci mode; from ich9 datasheet */
	pcicfgw16(c->pci, 0x90, 1<<6 | 1<<5);
}

static void
sbsetupahci(Pcidev *p)
{
	print("sbsetupahci: tweaking %.4ux ccru %.2ux ccrp %.2ux\n",
		p->did, p->ccru, p->ccrp);
	pcicfgw8(p, 0x40, pcicfgr8(p, 0x40) | 1);
	pcicfgw8(p, PciCCRu, 6);
	pcicfgw8(p, PciCCRp, 1);
	p->ccru = 6;
	p->ccrp = 1;
}

static int
esbenc(Ctlr *c)
{
	c->encsz = 1;
	c->enctx = (uint32_t*)(c->mmio + 0xa0);
	c->enctype = Eesb;
	c->enctx[0] = 0;
	return 0;
}

static int
ahciencinit(Ctlr *c)
{
	uint32_t type, sz, o, *bar;
	Ahba *h;

	h = c->hba;
	if(c->type == Tesb)
		return esbenc(c);
	if((h->cap & Hems) == 0)
		return -1;
	type = h->emctl & Emtype;
	switch(type){
	case Esgpio:
	case Eses2:
	case Esafte:
		return -1;
	case Elmt:
		break;
	default:
		return -1;
	}

	sz = h->emloc & 0xffff;
	o = h->emloc>>16;
	if(sz == 0 || o == 0)
		return -1;
	bar = c->lmmio;
	dprint("size = %.4lux; loc = %.4lux*4\n", sz, o);
	c->encsz = sz;
	c->enctx = bar + o;
	if((h->emctl & Xonly) == 0){
		if(h->emctl & Smb)
			c->encrx = bar + o;
		else
			c->encrx = bar + o*2;
	}
	c->enctype = type;
	return 0;
}

static int
didtype(Pcidev *p)
{
	int type;

	type = Tahci;
	switch(p->vid){
	default:
		return -1;
	case 0x8086:
		if((p->did & 0xffff) == 0x1e02)
			return Tich;		/* c210 */
		if((p->did & 0xffff) == 0x8c02)
			return Tich;		/* c220 */
		if((p->did & 0xffff) == 0x24d1)
			return Tich;		/* 82801eb/er */
		if((p->did & 0xffff) == 0x2653)
			return Tich;		/* 82801fbm */
		if((p->did & 0xfffc) == 0x2680)
			return Tesb;
		if((p->did & 0xfffb) == 0x27c1)
			return Tich;		/* 82801g[bh]m */
		if((p->did & 0xffff) == 0x2822)
			return Tich;		/* 82801 SATA RAID */
		if((p->did & 0xffff) == 0x2821)
			return Tich;		/* 82801h[roh] */
		if((p->did & 0xfffe) == 0x2824)
			return Tich;		/* 82801h[b] */
		if((p->did & 0xfeff) == 0x2829)
			return Tich;		/* ich8 */
		if((p->did & 0xfffe) == 0x2922)
			return Tich;		/* ich9 */
		if((p->did & 0xffff) == 0x3a02)
			return Tich;		/* 82801jd/do */
		if((p->did & 0xfefe) == 0x3a22)
			return Tich;		/* ich10, pch */
		if((p->did & 0xfff7) == 0x3b28)
			return Tich;		/* pchm */
		if((p->did & 0xfffe) == 0x3b22)
			return Tich;		/* pch */
		break;
	case 0x1002:
		if(p->ccru == 1 || p->ccrp != 1)
		if(p->did == 0x4380 || p->did == 0x4390)
			sbsetupahci(p);
		type = Tsb600;
		break;
	case 0x1106:
		/*
		 * unconfirmed report that the programming
		 * interface is set incorrectly.
		 */
		if(p->did == 0x3349)
			return Tahci;
		break;
	case 0x1022:
		/* Hudson SATA Controller [AHCI mode] */
		if(p->did == 0x7801)
			return Tahci;
		break;
	case 0x10de:
	case 0x1039:
	case 0x1b4b:
	case 0x11ab:
		break;
	case 0x197b:
	case 0x10b9:
		type = Tjmicron;
		break;
	}
	if(p->ccrb == Pcibcstore && p->ccru == 6 && p->ccrp == 1)
		return type;
	return -1;
}

static SDev*
iapnp(void)
{
	int i, n, nunit, type;
	uintptr io;
	Ctlr *c;
	Drive *d;
	Pcidev *p;
	SDev *s;
	static int done;

	if(done)
		return nil;
	done = 1;

	if(getconf("*noahci") != nil)
		return nil;

	if(getconf("*ahcidebug") != nil){
		debug = 1;
		datapi = 1;
	}

	memset(olds, 0xff, sizeof olds);
	p = nil;
	while((p = pcimatch(p, 0, 0)) != nil){
		if((type = didtype(p)) == -1)
			continue;
		io = p->mem[Abar].bar;
		if(io == 0 || (io & 1) != 0 || p->mem[Abar].size < 0x180)
			continue;
		io &= ~0xf;
		if(niactlr == NCtlr){
			print("iapnp: %s: too many controllers\n", tname[type]);
			break;
		}
		c = iactlr + niactlr;
		s = sdevs + niactlr;
		memset(c, 0, sizeof *c);
		memset(s, 0, sizeof *s);
		c->mmio = vmap(io, p->mem[Abar].size);
		if(c->mmio == 0){
			print("%s: address %#p in use did %.4ux\n",
				Tname(c), io, p->did);
			continue;
		}
		c->lmmio = (uint32_t*)c->mmio;
		c->pci = p;
		c->type = type;

		s->ifc = &sdiahciifc;
		s->idno = 'E';
		s->ctlr = c;
		c->sdev = s;

		ahcihandoff((Ahba*)c->mmio);
		if(intel(c) && p->did != 0x2681)
			iasetupahci(c);
//		ahcihbareset((Ahba*)c->mmio);
		nunit = ahciconf(c);
		if(intel(c) && iaahcimode(p) == -1 || nunit < 1){
			vunmap(c->mmio, p->mem[Abar].size);
			continue;
		}
		c->ndrive = s->nunit = nunit;

		/* map the drives -- they don't all need to be enabled. */
		memset(c->rawdrive, 0, sizeof c->rawdrive);
		n = 0;
		for(i = 0; i < NCtlrdrv; i++){
			d = c->rawdrive + i;
			d->portno = i;
			d->driveno = -1;
			d->sectors = 0;
			d->serial[0] = ' ';
			d->ctlr = c;
			if((c->hba->pi & 1<<i) == 0)
				continue;
			io = 0x100 + 0x80*i;
			if((io + 0x80) > p->mem[Abar].size)
				continue;
			d->port = (Aport*)(c->mmio + io);
			d->portc.p = d->port;
			d->portc.m = &d->portm;
			d->driveno = n++;
			snprint(d->name, sizeof d->name, "iahci%d.%d", niactlr, i);
			c->drive[d->driveno] = d;
			iadrive[niadrive + d->driveno] = d;
		}
		for(i = 0; i < n; i++){
			c->drive[i]->mode = DMautoneg;
			configdrive(c->drive[i]);
		}
		ahciencinit(c);

		niadrive += n;
		niactlr++;
		sdadddevs(s);
		i = (c->hba->cap >> 21) & 1;
		print("#S/%s: %s: sata-%s with %d ports\n", s->name,
			Tname(c), "I\0II" + i*2, nunit);
	}
	return nil;
}

static Htab ctab[] = {
	Aasp,	"asp",
	Aalpe ,	"alpe ",
	Adlae,	"dlae",
	Aatapi,	"atapi",
	Apste,	"pste",
	Afbsc,	"fbsc",
	Aesp,	"esp",
	Acpd,	"cpd",
	Ampsp,	"mpsp",
	Ahpcp,	"hpcp",
	Apma,	"pma",
	Acps,	"cps",
	Acr,	"cr",
	Afr,	"fr",
	Ampss,	"mpss",
	Apod,	"pod",
	Asud,	"sud",
	Ast,	"st",
};

static char*
capfmt(char *p, char *e, Htab *t, int n, uint32_t cap)
{
	uint32_t i;

	*p = 0;
	for(i = 0; i < n; i++)
		if(cap & t[i].bit)
			p = seprint(p, e, "%s ", t[i].name);
	return p;
}

static int
iarctl(SDunit *u, char *p, int l)
{
	char buf[32], *e, *op;
	Aport *o;
	Ctlr *c;
	Drive *d;

	if((c = u->dev->ctlr) == nil)
		return 0;
	d = c->drive[u->subno];
	o = d->port;

	e = p+l;
	op = p;
	if(d->state == Dready){
		p = seprint(p, e, "model\t%s\n", d->model);
		p = seprint(p, e, "serial\t%s\n", d->serial);
		p = seprint(p, e, "firm\t%s\n", d->firmware);
		if(d->wwn != 0)
			p = seprint(p, e, "wwn\t%ullx\n", d->wwn);
		p = seprint(p, e, "flag\t");
		p = pflag(p, e, &d->portm);
		p = seprint(p, e, "udma\t%d\n", d->portm.udma);
	}else
		p = seprint(p, e, "no disk present [%s]\n", diskstates[d->state]);
	serrstr(o->serror, buf, buf + sizeof buf - 1);
	p = seprint(p, e, "reg\ttask %lux cmd %lux serr %lux %s ci %lux is %lux "
		"sig %lux sstatus %.3lux\n", o->task, o->cmd, o->serror, buf,
		o->ci, o->isr, o->sig, o->sstatus);
	p = seprint(p, e, "cmd\t");
	p = capfmt(p, e, ctab, nelem(ctab), o->cmd);
	p = seprint(p, e, "\n");
	p = seprint(p, e, "mode\t%s %s\n", modes[d->mode], modes[maxmode(c)]);
	p = seprint(p, e, "geometry %llud %d\n", d->sectors, d->secsize);
	p = seprint(p, e, "alignment %d %d\n", 
		d->secsize<<d->portm.physshift, d->portm.physalign);
	p = seprint(p, e, "missirq\t%ud\n", c->missirq);
	return p - op;
}

static void
forcemode(Drive *d, char *mode)
{
	int i;

	for(i = 0; i < nelem(modes); i++)
		if(strcmp(mode, modes[i]) == 0)
			break;
	if(i == nelem(modes))
		i = 0;
	ilock(d);
	d->mode = i;
	iunlock(d);
}

static void
forcestate(Drive *d, char *state)
{
	int i;

	for(i = 0; i < nelem(diskstates); i++)
		if(strcmp(state, diskstates[i]) == 0)
			break;
	if(i == nelem(diskstates))
		error(Ebadctl);
	dstatus(d, i);
}

static int
runsettxmode(Drive *d, char *s)
{
	int i;
	Aportc *c;
	Aportm *m;

	c = &d->portc;
	m = &d->portm;

	i = 1;
	if(lockready(d) == 0){
		m->udma = atoi(s);
		if(settxmode(c, m->udma) == 0)
			i = 0;
	}
	qunlock(m);
	return i;
}


static int
iawctl(SDunit *u, Cmdbuf *cmd)
{
	char **f;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	f = cmd->f;

	if(strcmp(f[0], "mode") == 0)
		forcemode(d, f[1]? f[1]: "satai");
	else if(strcmp(f[0], "state") == 0)
		forcestate(d, f[1]? f[1]: "null");
	else if(strcmp(f[0], "txmode") == 0){
		if(runsettxmode(d, f[1]? f[1]: "0"))
			cmderror(cmd, "bad txmode / stuck port");
	}else
		cmderror(cmd, Ebadctl);
	return 0;
}

static char *
portr(char *p, char *e, uint32_t x)
{
	int i, a;

	p[0] = 0;
	a = -1;
	for(i = 0; i < 32; i++){
		if((x & (1<<i)) == 0){
			if(a != -1 && i - 1 != a)
				p = seprint(p, e, "-%d", i - 1);
			a = -1;
			continue;
		}
		if(a == -1){
			if(i > 0)
				p = seprint(p, e, ", ");
			p = seprint(p, e, "%d", a = i);
		}
	}
	if(a != -1 && i - 1 != a)
		p = seprint(p, e, "-%d", i - 1);
	return p;
}

static Htab htab[] = {
	H64a,	"64a",
	Hncq,	"ncq",
	Hsntf,	"ntf",
	Hmps,	"mps",
	Hss,	"ss",
	Halp,	"alp",
	Hal,	"led",
	Hclo,	"clo",
	Ham,	"am",
	Hpm,	"pm",
	Hfbs,	"fbs",
	Hpmb,	"pmb",
	Hssc,	"slum",
	Hpsc,	"pslum",
	Hcccs,	"coal",
	Hems,	"ems",
	Hxs,	"xs",
};

static Htab htab2[] = {
	Apts,	"apts",
	Nvmp,	"nvmp",
	Boh,	"boh",
};

static Htab emtab[] = {
	Pm,	"pm",
	Alhd,	"alhd",
	Xonly,	"xonly",
	Smb,	"smb",
	Esgpio,	"esgpio",
	Eses2,	"eses2",
	Esafte,	"esafte",
	Elmt,	"elmt",
};

static char*
iartopctl(SDev *s, char *p, char *e)
{
	char pr[25];
	uint32_t cap;
	Ahba *h;
	Ctlr *c;

	c = s->ctlr;
	h = c->hba;
	cap = h->cap;
	p = seprint(p, e, "sd%c ahci %s port %#p: ", s->idno, Tname(c), h);
	p = capfmt(p, e, htab, nelem(htab), cap);
	p = capfmt(p, e, htab2, nelem(htab2), h->cap2);
	p = capfmt(p, e, emtab, nelem(emtab), h->emctl);
	portr(pr, pr + sizeof pr, h->pi);
	return seprint(p, e,
		"iss %ld ncs %ld np %ld ghc %lux isr %lux pi %lux %s ver %lux\n",
		(cap>>20) & 0xf, (cap>>8) & 0x1f, 1 + (cap & 0x1f),
		h->ghc, h->isr, h->pi, pr, h->ver);
}

static int
iawtopctl(SDev *, Cmdbuf *cmd)
{
	int *v;
	char **f;

	f = cmd->f;
	v = 0;

	if(strcmp(f[0], "debug") == 0)
		v = &debug;
	else if(strcmp(f[0], "idprint") == 0)
		v = &prid;
	else if(strcmp(f[0], "aprint") == 0)
		v = &datapi;
	else if(strcmp(f[0], "ledprint") == 0)
		v = &dled;
	else
		cmderror(cmd, Ebadctl);

	switch(cmd->nf){
	default:
		cmderror(cmd, Ebadarg);
	case 1:
		*v ^= 1;
		break;
	case 2:
		if(f[1])
			*v = strcmp(f[1], "on") == 0;
		else
			*v ^= 1;
		break;
	}
	return 0;
}

SDifc sdiahciifc = {
	"ahci",

	iapnp,
	nil,		/* legacy */
	iaenable,
	iadisable,

	iaverify,
	iaonline,
	iario,
	iarctl,
	iawctl,

	ahcibio,
	nil,		/* probe */
	nil,		/* clear */
	iartopctl,
	iawtopctl,
	iaataio,
};
