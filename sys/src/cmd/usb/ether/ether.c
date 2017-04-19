#include <u.h>
#include <lib9.h>
#include <thread.h>

#include "usb.h"
#include "dat.h"

#include <9P2000.h>
#include <9p.h>
#include <ip.h>

typedef struct Tab Tab;
typedef struct Dq Dq;
typedef struct Conn Conn;
typedef struct Stats Stats;

enum
{
	Qroot,
	Qiface,
	Qclone,
	Qstats,
	Qaddr,
	Qndir,
	Qctl,
	Qdata,
	Qtype,
	Qmax,
};

struct Tab
{
	char *name;
	uint32_t mode;
};

Tab tab[] =
{
	"/",		DMDIR|0555,
	"etherU",	DMDIR|0555,	/* calling it *ether* makes snoopy(8) happy */
	"clone",	0666,
	"stats",	0666,
	"addr",	0444,
	"%ud",	DMDIR|0555,
	"ctl",		0666,
	"data",	0666,
	"type",	0444,
};

struct Dq
{
	QLock	ql;

	Dq		*next;
	Req		*r;
	Req		**rt;
	Block		*q;
	Block		**qt;

	int		size;

	int		nb;
};

struct Conn
{
	QLock	ql;

	int		used;
	int		type;
	int		prom;
	Dq		*dq;
};

struct Stats
{
	int		in;
	int		out;
};

Stats stats;
Conn conn[32];
int nconn = 0;

Dev *epin;
Dev *epout;

uint32_t time0;
static char *uname;

#define PATH(type, n)		((type)|((n)<<8))
#define TYPE(path)			(((uint32_t)(path) & 0x000000FF)>>0)
#define NUM(path)			(((uint32_t)(path) & 0xFFFFFF00)>>8)
#define NUMCONN(c)		(((uintptr_t)(c)-(uintptr_t)&conn[0])/sizeof(conn[0]))

static void
fillstat(Dir *d, uint64_t path)
{
	Tab *t;

	memset(d, 0, sizeof(*d));
	d->uid = estrdup9p(uname);
	d->gid = estrdup9p(uname);
	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	d->name = smprint(t->name, NUM(path));
	d->qid.type = t->mode>>24;
	d->mode = t->mode;
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	if(uname == nil)
		uname = estrdup9p(r->ifcall.uname);
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void* _)
{
	i += Qroot+1;
	if(i == Qiface){
		fillstat(d, i);
		return 0;
	}
	return -1;
}

static int
ifacegen(int i, Dir *d, void* _)
{
	i += Qiface+1;
	if(i < Qndir){
		fillstat(d, i);
		return 0;
	}
	i -= Qndir;
	if(i < nconn){
		fillstat(d, PATH(Qndir, i));
		return 0;
	}
	return -1;
}

static int
ndirgen(int i, Dir *d, void *aux)
{
	i += Qndir+1;
	if(i < Qmax){
		fillstat(d, PATH(i, NUMCONN(aux)));
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i, n;
	char buf[32];
	uint32_t path;

	path = fid->qid.path;
	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0){
		switch(TYPE(path)){
		case Qroot:
			return nil;
		case Qiface:
			qid->path = PATH(Qroot, 0);
			qid->type = tab[Qroot].mode>>24;
			return nil;
		case Qndir:
			qid->path = PATH(Qiface, 0);
			qid->type = tab[Qiface].mode>>24;
			return nil;
		default:
			return "bug in fswalk1";
		}
	}

	for(i = TYPE(path)+1; i<nelem(tab); i++){
		if(i==Qndir){
			n = atoi(name);
			snprint(buf, sizeof buf, "%d", n);
			if(n < nconn && strcmp(buf, name) == 0){
				qid->path = PATH(i, n);
				qid->type = tab[i].mode>>24;
				return nil;
			}
			break;
		}
		if(strcmp(name, tab[i].name) == 0){
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode>>24;
			return nil;
		}
		if(tab[i].mode&DMDIR)
			break;
	}
	return "directory entry not found";
}

static void
matchrq(Dq *d)
{
	Req *r;
	Block *b;

	while(r = d->r){
		int n;

		if((b = d->q) == nil)
			break;

		d->size -= BLEN(b);
		if((d->q = b->next) == nil)
			d->qt = &d->q;
		if((d->r = (Req*)r->aux) == nil)
			d->rt = &d->r;

		n = r->ifcall.count;
		if(n > BLEN(b))
			n = BLEN(b);
		memmove(r->ofcall.data, b->rp, n);
		r->ofcall.count = n;
		freeb(b);

		respond(r, nil);
	}
}

static void
readconndata(Req *r)
{
	Dq *d;

	d = r->fid->aux;
	qlock(&d->ql);
	if(d->q==nil && d->nb){
		qunlock(&d->ql);
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	r->aux = nil;
	*d->rt = r;
	d->rt = (Req**)&r->aux;
	matchrq(d);
	qunlock(&d->ql);
}

static void
writeconndata(Req *r)
{
	Dq *d;
	void *p;
	int n;
	Block *b;

	d = r->fid->aux;
	p = r->ifcall.data;
	n = r->ifcall.count;
	if((n == 11) && memcmp(p, "nonblocking", n)==0){
		d->nb = 1;
		goto out;
	}

	/* minimum frame length for rtl8150 */
	if(n < 60)
		n = 60;

	/* slack space for header and trailers */
	n += 44+16;

	b = allocb(n);

	/* header space */
	b->wp += 44;
	b->rp = b->wp;

	/* copy in the ethernet packet */
	memmove(b->wp, p, r->ifcall.count);
	b->wp += r->ifcall.count;

	etheriq(b, 0);

out:
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

#if 0
static char*
mac2str(uint8_t *m)
{
	int i;
	char *t = "0123456789abcdef";
	static char buf[13];
	buf[13] = 0;
	for(i=0; i<6; i++){
		buf[i*2] = t[m[i]>>4];
		buf[i*2+1] = t[m[i]&0xF];
	}
	return buf;
}
#endif

static void
fsread(Req *r)
{
	char buf[200];
	char e[ERRMAX];
	uint32_t path;

	path = r->fid->qid.path;

	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in fsread path=%lux", path);
		respond(r, e);
		break;

	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;

	case Qiface:
		dirread9p(r, ifacegen, nil);
		respond(r, nil);
		break;

	case Qstats:
		snprint(buf, sizeof(buf),
			"in: %d\n"
			"out: %d\n"
			"mbps: %d\n"
			"addr: %E\n",
			stats.in, stats.out, 10, macaddr);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qaddr:
		snprint(buf, sizeof(buf), "%E", macaddr);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qndir:
		dirread9p(r, ndirgen, &conn[NUM(path)]);
		respond(r, nil);
		break;

	case Qctl:
		snprint(buf, sizeof(buf), "%11d ", NUM(path));
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qtype:
		snprint(buf, sizeof(buf), "%11d ", conn[NUM(path)].type);
		readstr(r, buf);
		respond(r, nil);
		break;

	case Qdata:
		readconndata(r);
		break;
	}
}

static void
fswrite(Req *r)
{
	char e[ERRMAX];
	uint32_t path;
	char *p;
	int n;

	path = r->fid->qid.path;
	switch(TYPE(path)){
	case Qctl:
		n = r->ifcall.count;
		p = (char*)r->ifcall.data;
		if((n == 11) && memcmp(p, "promiscuous", 11)==0)
			conn[NUM(path)].prom = 1;
		if((n > 8) && memcmp(p, "connect ", 8)==0){
			char x[12];

			if(n - 8 >= sizeof(x)){
				respond(r, "invalid control msg");
				return;
			}

			p += 8;
			memcpy(x, p, n-8);
			x[n-8] = 0;

			conn[NUM(path)].type = atoi(p);
		}
		r->ofcall.count = n;
		respond(r, nil);
		break;
	case Qdata:
		writeconndata(r);
		break;
	default:
		snprint(e, sizeof e, "bug in fswrite path=%lux", path);
		respond(r, e);
	}
}

static void
fsopen(Req *r)
{
	static int need[4] = { 4, 2, 6, 1 };
	uint32_t path;
	int i, n;
	Tab *t;
	Dq *d;
	Conn *c;

	/*
	 * lib9p already handles the blatantly obvious.
	 * we just have to enforce the permissions we have set.
	 */
	path = r->fid->qid.path;
	t = &tab[TYPE(path)];
	n = need[r->ifcall.mode&3];
	if((n&t->mode) != n){
		respond(r, "permission denied");
		return;
	}

	d = nil;
	r->fid->aux = nil;

	switch(TYPE(path)){
	case Qclone:
		for(i=0; i<nelem(conn); i++){
			if(conn[i].used)
				continue;
			if(i >= nconn)
				nconn = i+1;
			path = PATH(Qctl, i);
			goto CaseConn;
		}
		respond(r, "out of connections");
		return;
	case Qdata:
		d = emalloc9p(sizeof(*d));
		memset(d, 0, sizeof(*d));
		d->qt = &d->q;
		d->rt = &d->r;
		r->fid->aux = d;
	case Qndir:
	case Qctl:
	case Qtype:
	CaseConn:
		c = &conn[NUM(path)];
		qlock(&c->ql);
		if(c->used++ == 0){
			c->type = 0;
			c->prom = 0;
		}
		if(d != nil){
			d->next = c->dq;
			c->dq = d;
		}
		qunlock(&c->ql);
		break;
	}

	r->fid->qid.path = path;
	r->ofcall.qid.path = path;
	respond(r, nil);
}

static void
fsflush(Req *r)
{
	Req *o, **p;
	Fid *f;
	Dq *d;

	o = r->oldreq;
	f = o->fid;
	if(TYPE(f->qid.path) == Qdata){
		d = f->aux;
		qlock(&d->ql);
		for(p=&d->r; *p; p=(Req**)&((*p)->aux)){
			if(*p == o){
				if((*p = (Req*)o->aux) == nil)
					d->rt = p;
				r->oldreq = nil;
				respond(o, "interrupted");
				break;
			}
		}
		qunlock(&d->ql);
	}
	respond(r, nil);
}


static void
fsdestroyfid(Fid *fid)
{
	Conn *c;
	Dq **x, *d;
	Block *b;

	if(TYPE(fid->qid.path) >= Qndir){
		c = &conn[NUM(fid->qid.path)];
		qlock(&c->ql);
		if(d = fid->aux){
			fid->aux = nil;
			for(x=&c->dq; *x; x=&((*x)->next)){
				if(*x == d){
					*x = d->next;
					break;
				}
			}
			while(b = d->q){
				d->q = b->next;
				freeb(b);
			}
			free(d);
		}
		if(TYPE(fid->qid.path) == Qctl)
			c->prom = 0;
		c->used--;
		qunlock(&c->ql);
	}
}

static void
setalt(Dev *d, int ifcid, int altid)
{
	if(usbcmd(d, Rh2d|Rstd|Riface, Rsetiface, altid, ifcid, nil, 0) < 0)
		dprint(2, "%s: setalt ifc %d alt %d: %r\n", argv0, ifcid, altid);
}

static int
ifaceinit(Dev *d, Iface *ifc, int *ei, int *eo)
{
	int i, epin, epout;
	Ep *ep;

	if(ifc == nil)
		return -1;

	epin = epout = -1;
	for(i = 0; (epin < 0 || epout < 0) && i < nelem(ifc->ep); i++)
		if((ep = ifc->ep[i]) != nil && ep->type == Ebulk){
			if(ep->dir == Eboth || ep->dir == Ein)
				if(epin == -1)
					epin =  ep->id;
			if(ep->dir == Eboth || ep->dir == Eout)
				if(epout == -1)
					epout = ep->id;
		}
	if(epin == -1 || epout == -1)
		return -1;

	for(i = 0; i < nelem(ifc->altc); i++)
		if(ifc->altc[i] != nil)
			setalt(d, ifc->id, i);

	*ei = epin;
	*eo = epout;
	return 0;
}

int
findendpoints(Dev *d, int *ei, int *eo)
{
	int i, j, ctlid, datid;
	Iface *ctlif, *datif;
	Usbdev *ud;
	Desc *desc;
	Conf *c;

	ud = d->usb;
	*ei = *eo = -1;

	/* look for union descriptor with ethernet ctrl interface */
	for(i = 0; i < nelem(ud->ddesc); i++){
		if((desc = ud->ddesc[i]) == nil)
			continue;
		if(desc->data.bLength < 5 || desc->data.bbytes[0] != Cdcunion)
			continue;

		ctlid = desc->data.bbytes[1];
		datid = desc->data.bbytes[2];

		if((c = desc->conf) == nil)
			continue;

		ctlif = datif = nil;
		for(j = 0; j < nelem(c->iface); j++){
			if(c->iface[j] == nil)
				continue;
			if(c->iface[j]->id == ctlid)
				ctlif = c->iface[j];
			if(c->iface[j]->id == datid)
				datif = c->iface[j];

			if(datif != nil && ctlif != nil){
				if(Subclass(ctlif->csp) == Scether)
					if(ifaceinit(d, datif, ei, eo) != -1)
						return 0;
				break;
			}
		}		
	}

	/* try any other one that seems to be ok */
	for(i = 0; i < nelem(ud->conf); i++)
		if((c = ud->conf[i]) != nil)
			for(j = 0; j < nelem(c->iface); j++)
				if(ifaceinit(d,c->iface[j],ei,eo) != -1)
					return 0;

	return -1;
}

static int
inote(void * _, char *msg)
{
	if(strstr(msg, "interrupt"))
		return 1;
	return 0;
}

void
etheriq(Block *b, int wire)
{
	int i, t;
	Block *q;
	Conn *c;
	Dq *d;
	Ehdr *h;

	if(BLEN(b) < Ehdrsz){
		freeb(b);
		return;
	}

	h = (Ehdr*)b->rp;
	t = (h->type[0]<<8)|h->type[1];

	for(i=0; i<nconn; i++){
		c = &conn[i];
		qlock(&c->ql);
		if(!c->used)
			goto next;
		if(c->type > 0)
			if(c->type != t)
				goto next;
		if(!c->prom && !(h->d[0]&1))
			if(memcmp(h->d, macaddr, sizeof(macaddr)))
				goto next;
		for(d=c->dq; d; d=d->next){
			if(d->size > 100000)
				continue;
			if(c->type == -2) {
				q = copyblock(b, 64);
			} else if(wire && b->ref.ref == 1) {
				incref(&b->ref);
				q = b;
			} else {
				q = copyblock(b, BLEN(b));
			}
			qlock(&d->ql);
			q->next = nil;
			*d->qt = q;
			d->qt = &q->next;
			d->size += BLEN(q);
			matchrq(d);
			qunlock(&d->ql);
		}
next:
		qunlock(&c->ql);
	}
	if(wire) {
		freeb(b);
		stats.in++;
	} else {
		/* transmit frees buffer */
		(*eptransmit)(epout, b);
		stats.out++;
	}
}

static void
usbreadproc(void * _)
{
	char err[ERRMAX];
	int nerr;

	atnotify(inote, 1);

	threadsetname("usbreadproc");

	nerr = 0;
	for(;;){
		/* receive allocates buffer and calls etheriq(b, 1); */
		if((*epreceive)(epin) < 0){
			rerrstr(err, sizeof(err));
			if(strstr(err, "interrupted") || strstr(err, "timed out"))
				continue;
			fprint(2, "usbreadproc: %s\n", err);
			if(++nerr < 3)
				continue;
			threadexitsall(err);
		}
		nerr = 0;
	}
}

Srv fs = 
{
.attach=		fsattach,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.open=		fsopen,
.read=		fsread,
.write=		fswrite,
.stat=		fsstat,
.flush=		fsflush,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-dD] [-t type] [-a addr] devid\n", argv0);
	exits("usage");
}

extern int aueinit(Dev *);
extern int a88178init(Dev *);
extern int a88772init(Dev *);
extern int smscinit(Dev *);
extern int cdcinit(Dev *);
extern int urlinit(Dev *);
extern int rndisinit(Dev *);

static struct {
	char *name;
	int (*init)(Dev*);
} ethertype[] = {
	"cdc",		cdcinit,
	"smsc",		smscinit,
	"a88178",	a88178init,
	"a88772",	a88772init,
	"aue",		aueinit,
	"url",		urlinit,
	"rndis",	rndisinit,
};

void
threadmain(int argc, char **argv)
{
	char s[64], *t;
	int et, ei, eo;
	Dev *d;

	fmtinstall('E', eipfmt);

	et = 0;	/* CDC */
	ARGBEGIN {
	case 'd':
		debug = 1;
		break;
	case 'D':
		chatty9p++;
		break;
	case 'a':
		setmac = 1;
		parseether(macaddr, EARGF(usage()));
		break;
	case 't':
		t = EARGF(usage());
		for(et=0; et<nelem(ethertype); et++)
			if(strcmp(t, ethertype[et].name) == 0)
				break;
		if(et == nelem(ethertype)){
			fprint(2, "unsupported ethertype -t %s, supported types are: ", t);
			for(et=0; et<nelem(ethertype); et++)
				fprint(2, "%s ", ethertype[et].name);
			fprint(2, "\n");
			exits("usage");
		}
		break;
	default:
		usage();
	} ARGEND;

	if(argc != 1)
		usage();

	if((d = getdev(*argv)) == nil)
		sysfatal("getdev: %r");
	if(findendpoints(d, &ei, &eo) < 0)
		sysfatal("no endpoints found");

	werrstr("");
	if((*ethertype[et].init)(d) < 0)
		sysfatal("%s init failed: %r", ethertype[et].name);
	if(epreceive == nil || eptransmit == nil)
		sysfatal("bug in init");

	if((epin = openep(d, ei)) == nil)
		sysfatal("openep: %r");
	if(ei == eo){
		incref(&epin->ref);
		epout = epin;
		opendevdata(epin, ORDWR);
	} else {
		if((epout = openep(d, eo)) == nil)
			sysfatal("openep: %r");
		opendevdata(epin, OREAD);
		opendevdata(epout, OWRITE);
	}

	proccreate(usbreadproc, nil, 8*1024);

	atnotify(inote, 1);
	time0 = time(0);
	tab[Qiface].name = smprint("etherU%s", d->hname);
	snprint(s, sizeof(s), "%d.ether", d->id);
	closedev(d);
	threadpostsharesrv(&fs, nil, "usbnet", s);

	threadexits(0);
}

Block*
allocb(int size)
{
	Block *b;

	b = emalloc9p(sizeof(*b) + size);
	b->lim = b->base + size;
	b->rp = b->base;
	b->wp = b->base;
	b->next = nil;
	b->ref.ref = 1;
	return b;
}

void
freeb(Block *b)
{
	if(decref(&b->ref) == 0)
		free(b);
}

Block*
copyblock(Block *b, int count)
{
	Block *nb;

	if(count > BLEN(b))
		count = BLEN(b);
	nb = allocb(count);
	memmove(nb->wp, b->rp, count);
	nb->wp += count;
	return nb;
}
