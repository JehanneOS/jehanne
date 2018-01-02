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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct Pipe	Pipe;
struct Pipe
{
	QLock;
	Pipe	*next;
	int	ref;
	uint32_t	path;
	Queue	*q[2];
	int	qref[2];
};

struct
{
	Lock		l;
	uint32_t	path;
} pipealloc;

enum
{
	Qdir,
	Qdata0,
	Qdata1,

	PIPEQSIZE = 256*KiB,
};

Dirtab pipedir[] =
{
	".",		{Qdir,0,QTDIR},	0,		DMDIR|0500,
	"data",		{Qdata0},	0,		0600,
	"data1",	{Qdata1},	0,		0600,
};
#define NPIPEDIR 3

#define PIPETYPE(x)	(((unsigned)x)&0x1f)
#define PIPEID(x)	((((unsigned)x))>>5)
#define PIPEQID(i, t)	((((unsigned)i)<<5)|(t))

/*
 *  create a pipe, no streams are created until an open
 */
static Chan*
pipeattach(Chan *c, Chan *ac, char *spec, int flags)
{
	Pipe *p;

	c = devattach('|', spec);
	p = jehanne_malloc(sizeof(Pipe));
	if(p == 0)
		exhausted("memory");
	p->ref = 1;

	p->q[0] = qopen(PIPEQSIZE, 0, 0, 0);
	if(p->q[0] == 0){
		jehanne_free(p);
		exhausted("memory");
	}
	p->q[1] = qopen(PIPEQSIZE, 0, 0, 0);
	if(p->q[1] == 0){
		jehanne_free(p->q[0]);
		jehanne_free(p);
		exhausted("memory");
	}

	lock(&pipealloc.l);
	p->path = ++pipealloc.path;
	unlock(&pipealloc.l);

	mkqid(&c->qid, PIPEQID(2*p->path, Qdir), 0, QTDIR);
	c->aux = p;
	c->devno = 0;
	return c;
}

static int
pipegen(Chan *c, char* _1, Dirtab *tab, int ntab, int i, Dir *dp)
{
	Qid q;
	int len;
	Pipe *p;

	if(i == DEVDOTDOT){
		devdir(c, c->qid, "#|", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	i++;	/* skip . */
	if(tab==0 || i>=ntab)
		return -1;

	tab += i;
	p = c->aux;
	switch((uint32_t)tab->qid.path){
	case Qdata0:
		len = qlen(p->q[0]);
		break;
	case Qdata1:
		len = qlen(p->q[1]);
		break;
	default:
		len = tab->length;
		break;
	}
	mkqid(&q, PIPEQID(PIPEID(c->qid.path), tab->qid.path), 0, QTFILE);
	devdir(c, q, tab->name, len, eve, tab->perm, dp);
	return 1;
}


static Walkqid*
pipewalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *wq;
	Pipe *p;

	wq = devwalk(c, nc, name, nname, pipedir, NPIPEDIR, pipegen);
	if(wq != nil && wq->clone != nil && wq->clone != c){
		p = c->aux;
		qlock(p);
		p->ref++;
		if(c->flag & COPEN){
			jehanne_print("channel open in pipewalk\n");
			switch(PIPETYPE(c->qid.path)){
			case Qdata0:
				p->qref[0]++;
				break;
			case Qdata1:
				p->qref[1]++;
				break;
			}
		}
		qunlock(p);
	}
	return wq;
}

static long
pipestat(Chan *c, uint8_t *db, long n)
{
	Pipe *p;
	Dir dir;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdir:
		devdir(c, c->qid, ".", 0, eve, DMDIR|0555, &dir);
		break;
	case Qdata0:
		devdir(c, c->qid, "data", qlen(p->q[0]), eve, 0600, &dir);
		break;
	case Qdata1:
		devdir(c, c->qid, "data1", qlen(p->q[1]), eve, 0600, &dir);
		break;
	default:
		panic("pipestat");
	}
	n = jehanne_convD2M(&dir, db, n);
	if(n < BIT16SZ)
		error(Eshortstat);
	return n;
}

/*
 *  if the stream doesn't exist, create it
 */
static Chan*
pipeopen(Chan *c, unsigned long omode)
{
	Pipe *p;

	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Ebadarg);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	p = c->aux;
	qlock(p);
	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		p->qref[0]++;
		break;
	case Qdata1:
		p->qref[1]++;
		break;
	}
	qunlock(p);

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	c->iounit = qiomaxatomic;
	return c;
}

static void
pipeclose(Chan *c)
{
	Pipe *p;

	p = c->aux;
	qlock(p);

	if(c->flag & COPEN){
		/*
		 *  closing either side hangs up the stream
		 */
		switch(PIPETYPE(c->qid.path)){
		case Qdata0:
			p->qref[0]--;
			if(p->qref[0] == 0){
				qhangup(p->q[1], 0);
				qclose(p->q[0]);
			}
			break;
		case Qdata1:
			p->qref[1]--;
			if(p->qref[1] == 0){
				qhangup(p->q[0], 0);
				qclose(p->q[1]);
			}
			break;
		}
	}


	/*
	 *  if both sides are closed, they are reusable
	 */
	if(p->qref[0] == 0 && p->qref[1] == 0){
		qreopen(p->q[0]);
		qreopen(p->q[1]);
	}

	/*
	 *  free the structure on last close
	 */
	p->ref--;
	if(p->ref == 0){
		qunlock(p);
		jehanne_free(p->q[0]);
		jehanne_free(p->q[1]);
		jehanne_free(p);
	} else
		qunlock(p);
}

static long
piperead(Chan *c, void *va, long n, int64_t _1)
{
	Pipe *p;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdir:
		return devdirread(c, va, n, pipedir, NPIPEDIR, pipegen);
	case Qdata0:
		return qread(p->q[0], va, n);
	case Qdata1:
		return qread(p->q[1], va, n);
	default:
		panic("piperead");
	}
	return -1;	/* not reached */
}

static Block*
pipebread(Chan *c, long n, int64_t offset)
{
	Pipe *p;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		return qbread(p->q[0], n);
	case Qdata1:
		return qbread(p->q[1], n);
	}

	return devbread(c, n, offset);
}

/*
 *  a write to a closed pipe causes a note to be sent to
 *  the process.
 */
static long
pipewrite(Chan *c, void *va, long n, int64_t _1)
{
	Pipe *p;
	Queue *q;

	if(!islo())
		jehanne_print("pipewrite hi %#p\n", getcallerpc());

	p = c->aux;
	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		q = p->q[1];
		break;
	case Qdata1:
		q = p->q[0];
		break;
	default:
		panic("pipewrite");
	}

	if(waserror()) {
		/* avoid notes when pipe is a mounted queue */
		if((c->flag & CMSG) == 0
		&& (qstate(q) & Qclosed)
		&& (jehanne_strcmp(Ehungup, up->errstr) == 0)) // do not obscure other errors
			postnote(up, 1, "sys: write on closed pipe", NUser);
		nexterror();
	}

	n = qwrite(q, va, n);
	poperror();
	return n;
}

static long
pipebwrite(Chan *c, Block *bp, int64_t _1)
{
	long n;
	Pipe *p;
	Queue *q;

	p = c->aux;
	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		q = p->q[1];
		break;
	case Qdata1:
		q = p->q[0];
		break;
	default:
		panic("pipebwrite");
	}

	if(waserror()) {
		/* avoid notes when pipe is a mounted queue */
		if((c->flag & CMSG) == 0
		&& (qstate(q) & Qclosed)
		&& (jehanne_strcmp(Ehungup, up->errstr) == 0)) // do not obscure other errors
			postnote(up, 1, "sys: write on closed pipe", NUser);
		nexterror();
	}

	n = qbwrite(q, bp);
	poperror();
	return n;
}

Dev pipedevtab = {
	'|',
	"pipe",

	devreset,
	devinit,
	devshutdown,
	pipeattach,
	pipewalk,
	pipestat,
	pipeopen,
	devcreate,
	pipeclose,
	piperead,
	pipebread,
	pipewrite,
	pipebwrite,
	devremove,
	devwstat,
};
