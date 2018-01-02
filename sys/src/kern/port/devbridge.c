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
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

/*
 * IPv4 Ethernet bridge
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../ip/ip.h"
#include "../port/netif.h"
#include "../port/error.h"
#include "etherif.h"

extern Dev* devtab[];

typedef struct Bridge 	Bridge;
typedef struct Port 	Port;
typedef struct Centry	Centry;
typedef struct Iphdr	Iphdr;
typedef struct Tcphdr	Tcphdr;

enum
{
	Qtopdir=	1,		/* top level directory */

	Qbridgedir,			/* bridge* directory */
	Qbctl,
	Qstats,
	Qcache,
	Qlog,

	Qportdir,			/* directory for a protocol */
	Qpctl,
	Qlocal,
	Qstatus,

	MaxQ,

	Maxbridge=	4,
	Maxport=	128,		// power of 2
	CacheHash=	257,		// prime
	CacheLook=	5,		// how many cache entries to examine
	CacheSize=	(CacheHash+CacheLook-1),
	CacheTimeout=	5*60,		// timeout for cache entry in seconds

	TcpMssMax = 1300,		// max desirable Tcp MSS value
	TunnelMtu = 1400,
};

static Dirtab bridgedirtab[]={
	"ctl",		{Qbctl},	0,	0666,
	"stats",	{Qstats},	0,	0444,
	"cache",	{Qcache},	0,	0444,
	"log",		{Qlog},		0,	0666,
};

static Dirtab portdirtab[]={
	"ctl",		{Qpctl},	0,	0666,
	"local",	{Qlocal},	0,	0444,
	"status",	{Qstatus},	0,	0444,
};

enum {
	Logcache=	(1<<0),
	Logmcast=	(1<<1),
};

// types of interfaces
enum
{
	Tether,
	Ttun,
};

static Logflag logflags[] =
{
	{ "cache",	Logcache, },
	{ "multicast",	Logmcast, },
	{ nil,		0, },
};

static Dirtab	*dirtab[MaxQ];

#define TYPE(x) 	(((uint32_t)(x).path) & 0xff)
#define PORT(x) 	((((uint32_t)(x).path) >> 8)&(Maxport-1))
#define QID(x, y) 	(((x)<<8) | (y))

struct Centry
{
	uint8_t	d[Eaddrlen];
	int	port;
	int32_t	expire;		// entry expires this many seconds after bootime
	int32_t	src;
	int32_t	dst;
};

struct Bridge
{
	QLock		ql;
	int		nport;
	Port		*port[Maxport];
	Centry		cache[CacheSize];
	uint32_t	hit;
	uint32_t	miss;
	uint32_t	copy;
	int32_t		delay0;		// constant microsecond delay per packet
	int32_t		delayn;		// microsecond delay per byte
	int		tcpmss;		// modify tcpmss value

	Log;
};

struct Port
{
	int	id;
	Bridge	*bridge;
	int	ref;
	int	closed;

	Chan	*data[2];	// channel to data

	Proc	*readp;		// read proc

	// the following uniquely identifies the port
	int	type;
	char	name[KNAMELEN];

	// owner hash - avoids bind/unbind races
	uint32_t	ownhash;

	// various stats
	int	in;		// number of packets read
	int	inmulti;	// multicast or broadcast
	int	inunknown;	// unknown address
	int	out;		// number of packets read
	int	outmulti;	// multicast or broadcast
	int	outunknown;	// unknown address
	int	outfrag;	// fragmented the packet
	int	nentry;		// number of cache entries for this port
};

enum {
	IP_TCPPROTO	= 6,
	EOLOPT		= 0,
	NOOPOPT		= 1,
	MSSOPT		= 2,
	MSS_LENGTH	= 4,		/* Mean segment size */
	SYN		= 0x02,		/* Pkt. is synchronise */
	IPHDR		= 20,		/* sizeof(Iphdr) */
};

struct Iphdr
{
	uint8_t	vihl;		/* Version and header length */
	uint8_t	tos;		/* Type of service */
	uint8_t	length[2];	/* packet length */
	uint8_t	id[2];		/* ip->identification */
	uint8_t	frag[2];	/* Fragment information */
	uint8_t	ttl;		/* Time to live */
	uint8_t	proto;		/* Protocol */
	uint8_t	cksum[2];	/* Header checksum */
	uint8_t	src[4];		/* IP source */
	uint8_t	dst[4];		/* IP destination */
};

struct Tcphdr
{
	uint8_t	sport[2];
	uint8_t	dport[2];
	uint8_t	seq[4];
	uint8_t	ack[4];
	uint8_t	flag[2];
	uint8_t	win[2];
	uint8_t	cksum[2];
	uint8_t	urg[2];
};

static Bridge bridgetab[Maxbridge];

static int	bridgegen(Chan *c, char*, Dirtab*, int, int s, Dir *dp);
static void	portbind(Bridge *b, int argc, char *argv[]);
static void	portunbind(Bridge *b, int argc, char *argv[]);
static void	etherread(void *a);
static char	*cachedump(Bridge *b);
static void	portfree(Port *port);
static void	cacheflushport(Bridge *b, int port);
static void	etherwrite(Port *port, Block *bp);

static void
bridgeinit(void)
{
	int i;
	Dirtab *dt;

	// setup dirtab with non directory entries
	for(i=0; i<nelem(bridgedirtab); i++) {
		dt = bridgedirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}
	for(i=0; i<nelem(portdirtab); i++) {
		dt = portdirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}
}

static Chan*
bridgeattach(Chan *c, Chan *ac, char *spec, int flags)
{
	int dev;

	dev = jehanne_atoi(spec);
	if(dev<0 || dev >= Maxbridge)
		error("bad specification");

	c = devattach('B', spec);
	mkqid(&c->qid, QID(0, Qtopdir), 0, QTDIR);
	c->devno = dev;
	return c;
}

static Walkqid*
bridgewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, (Dirtab*)0, 0, bridgegen);
}

static long
bridgestat(Chan* c, uint8_t* db, long n)
{
	return devstat(c, db, n, (Dirtab *)0, 0L, bridgegen);
}

static Chan*
bridgeopen(Chan* c, unsigned long omode)
{
	Bridge *b;

	omode &= 3;

	b = bridgetab + c->devno;

	switch(TYPE(c->qid)) {
	default:
		break;
	case Qlog:
		logopen(b);
		break;
	case Qcache:
		c->aux = cachedump(b);
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
bridgeclose(Chan* c)
{
	Bridge *b  = bridgetab + c->devno;

	switch(TYPE(c->qid)) {
	case Qcache:
		if(c->flag & COPEN)
			jehanne_free(c->aux);
		break;
	case Qlog:
		if(c->flag & COPEN)
			logclose(b);
		break;
	}
}

static long
bridgeread(Chan *c, void *a, long n, int64_t off)
{
	char buf[256];
	Bridge *b = bridgetab + c->devno;
	Port *port;
	int i, ingood, outgood;

	USED(off);
	switch(TYPE(c->qid)) {
	default:
		error(Eperm);
	case Qtopdir:
	case Qbridgedir:
	case Qportdir:
		return devdirread(c, a, n, 0, 0, bridgegen);
	case Qlog:
		return logread(b, a, off, n);
	case Qstatus:
		qlock(&b->ql);
		port = b->port[PORT(c->qid)];
		if(port == 0)
			jehanne_strcpy(buf, "unbound\n");
		else {
			i = 0;
			switch(port->type) {
			default:
				panic("bridgeread: unknown port type: %d",
					port->type);
			case Tether:
				i += jehanne_snprint(buf+i, sizeof(buf)-i, "ether %s: ", port->name);
				break;
			case Ttun:
				i += jehanne_snprint(buf+i, sizeof(buf)-i, "tunnel %s: ", port->name);
				break;
			}
			ingood = port->in - port->inmulti - port->inunknown;
			outgood = port->out - port->outmulti - port->outunknown;
			i += jehanne_snprint(buf+i, sizeof(buf)-i,
				"in=%d(%d:%d:%d) out=%d(%d:%d:%d:%d)\n",
				port->in, ingood, port->inmulti, port->inunknown,
				port->out, outgood, port->outmulti,
				port->outunknown, port->outfrag);
			USED(i);
		}
		n = readstr(off, a, n, buf);
		qunlock(&b->ql);
		return n;
	case Qbctl:
		jehanne_snprint(buf, sizeof(buf), "%s tcpmss\ndelay %ld %ld\n",
			b->tcpmss ? "set" : "clear", b->delay0, b->delayn);
		n = readstr(off, a, n, buf);
		return n;
	case Qcache:
		n = readstr(off, a, n, c->aux);
		return n;
	case Qstats:
		jehanne_snprint(buf, sizeof(buf), "hit=%uld miss=%uld copy=%uld\n",
			b->hit, b->miss, b->copy);
		n = readstr(off, a, n, buf);
		return n;
	}
}

static void
bridgeoption(Bridge *b, char *option, int value)
{
	if(jehanne_strcmp(option, "tcpmss") == 0)
		b->tcpmss = value;
	else
		error("unknown bridge option");
}


static long
bridgewrite(Chan *c, void *a, long n, int64_t off)
{
	Bridge *b = bridgetab + c->devno;
	Cmdbuf *cb;
	char *arg0, *p;

	USED(off);
	switch(TYPE(c->qid)) {
	default:
		error(Eperm);
	case Qbctl:
		cb = parsecmd(a, n);
		qlock(&b->ql);
		if(waserror()) {
			qunlock(&b->ql);
			jehanne_free(cb);
			nexterror();
		}
		if(cb->nf == 0)
			error("short write");
		arg0 = cb->f[0];
		if(jehanne_strcmp(arg0, "bind") == 0) {
			portbind(b, cb->nf-1, cb->f+1);
		} else if(jehanne_strcmp(arg0, "unbind") == 0) {
			portunbind(b, cb->nf-1, cb->f+1);
		} else if(jehanne_strcmp(arg0, "cacheflush") == 0) {
			log(b, Logcache, "cache flush\n");
			jehanne_memset(b->cache, 0, CacheSize*sizeof(Centry));
		} else if(jehanne_strcmp(arg0, "set") == 0) {
			if(cb->nf != 2)
				error("usage: set option");
			bridgeoption(b, cb->f[1], 1);
		} else if(jehanne_strcmp(arg0, "clear") == 0) {
			if(cb->nf != 2)
				error("usage: clear option");
			bridgeoption(b, cb->f[1], 0);
		} else if(jehanne_strcmp(arg0, "delay") == 0) {
			if(cb->nf != 3)
				error("usage: delay delay0 delayn");
			b->delay0 = jehanne_strtol(cb->f[1], nil, 10);
			b->delayn = jehanne_strtol(cb->f[2], nil, 10);
		} else
			error("unknown control request");
		poperror();
		qunlock(&b->ql);
		jehanne_free(cb);
		return n;
	case Qlog:
		cb = parsecmd(a, n);
		p = logctl(b, cb->nf, cb->f, logflags);
		jehanne_free(cb);
		if(p != nil)
			error(p);
		return n;
	}
}

static int
bridgegen(Chan *c, char* _1, Dirtab* _2, int _3, int s, Dir *dp)
{
	Bridge *b = bridgetab + c->devno;
	int type = TYPE(c->qid);
	Dirtab *dt;
	Qid qid;

	if(s  == DEVDOTDOT){
		switch(TYPE(c->qid)){
		case Qtopdir:
		case Qbridgedir:
			jehanne_snprint(up->genbuf, sizeof(up->genbuf), "#B%ld", c->devno);
			mkqid(&qid, Qtopdir, 0, QTDIR);
			devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
			break;
		case Qportdir:
			jehanne_snprint(up->genbuf, sizeof(up->genbuf), "bridge%ld", c->devno);
			mkqid(&qid, Qbridgedir, 0, QTDIR);
			devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
			break;
		default:
			panic("bridgewalk %llux", c->qid.path);
		}
		return 1;
	}

	switch(type) {
	default:
		/* non-directory entries end up here */
		if(c->qid.type & QTDIR)
			panic("bridgegen: unexpected directory");
		if(s != 0)
			return -1;
		dt = dirtab[TYPE(c->qid)];
		if(dt == nil)
			panic("bridgegen: unknown type: %lud", TYPE(c->qid));
		devdir(c, c->qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	case Qtopdir:
		if(s != 0)
			return -1;
		jehanne_snprint(up->genbuf, sizeof(up->genbuf), "bridge%ld", c->devno);
		mkqid(&qid, QID(0, Qbridgedir), 0, QTDIR);
		devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
		return 1;
	case Qbridgedir:
		if(s<nelem(bridgedirtab)) {
			dt = bridgedirtab+s;
			devdir(c, dt->qid, dt->name, dt->length, eve, dt->perm, dp);
			return 1;
		}
		s -= nelem(bridgedirtab);
		if(s >= b->nport)
			return -1;
		mkqid(&qid, QID(s, Qportdir), 0, QTDIR);
		jehanne_snprint(up->genbuf, sizeof(up->genbuf), "%d", s);
		devdir(c, qid, up->genbuf, 0, eve, 0555, dp);
		return 1;
	case Qportdir:
		if(s>=nelem(portdirtab))
			return -1;
		dt = portdirtab+s;
		mkqid(&qid, QID(PORT(c->qid),TYPE(dt->qid)), 0, QTFILE);
		devdir(c, qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	}
}

// assumes b is locked
static void
portbind(Bridge *b, int argc, char *argv[])
{
	Port *port;
	Chan *ctl;
	int type = 0, i, n;
	uint32_t ownhash;
	char *dev, *dev2 = nil, *p;
	char buf[100], name[KNAMELEN], path[8*KNAMELEN];
	static char usage[] = "usage: bind ether|tunnel name ownhash dev [dev2]";

	jehanne_memset(name, 0, KNAMELEN);
	if(argc < 4)
		error(usage);
	if(jehanne_strcmp(argv[0], "ether") == 0) {
		if(argc != 4)
			error(usage);
		type = Tether;
		jehanne_strncpy(name, argv[1], KNAMELEN-1);
		name[KNAMELEN-1] = 0;
//		parseaddr(addr, argv[1], Eaddrlen);
	} else if(jehanne_strcmp(argv[0], "tunnel") == 0) {
		if(argc != 5)
			error(usage);
		type = Ttun;
		jehanne_strncpy(name, argv[1], KNAMELEN-1);
		name[KNAMELEN-1] = 0;
//		parseip(addr, argv[1]);
		dev2 = argv[4];
	} else
		error(usage);
	ownhash = jehanne_strtoul(argv[2], 0, 0);
	dev = argv[3];
	for(i=0; i<b->nport; i++) {
		port = b->port[i];
		if(port != nil && port->type == type &&
		    jehanne_memcmp(port->name, name, KNAMELEN) == 0)
			error("port in use");
	}
	for(i=0; i<Maxport; i++)
		if(b->port[i] == nil)
			break;
	if(i == Maxport)
		error("no more ports");
	port = smalloc(sizeof(Port));
	port->ref = 1;
	port->id = i;
	port->ownhash = ownhash;

	if(waserror()) {
		portfree(port);
		nexterror();
	}
	port->type = type;
	jehanne_memmove(port->name, name, KNAMELEN);
	switch(port->type) {
	default:
		panic("portbind: unknown port type: %d", type);
	case Tether:
		jehanne_snprint(path, sizeof(path), "%s/clone", dev);
		ctl = namec(path, Aopen, ORDWR, 0);
		if(waserror()) {
			cclose(ctl);
			nexterror();
		}
		// check addr?

		// get directory name
		n = devtab[ctl->qid.type]->read(ctl, buf, sizeof(buf), 0);
		buf[n] = 0;
		for(p = buf; *p == ' '; p++)
			;
		jehanne_snprint(path, sizeof(path), "%s/%lud/data", dev, jehanne_strtoul(p, 0, 0));

		// setup connection to be promiscuous
		jehanne_snprint(buf, sizeof(buf), "connect -1");
		devtab[ctl->qid.type]->write(ctl, buf, jehanne_strlen(buf), 0);
		jehanne_snprint(buf, sizeof(buf), "promiscuous");
		devtab[ctl->qid.type]->write(ctl, buf, jehanne_strlen(buf), 0);
		jehanne_snprint(buf, sizeof(buf), "bridge");
		devtab[ctl->qid.type]->write(ctl, buf, jehanne_strlen(buf), 0);

		// open data port
		port->data[0] = namec(path, Aopen, ORDWR, 0);
		// dup it
		incref(&port->data[0]->r);
		port->data[1] = port->data[0];

		poperror();
		cclose(ctl);

		break;
	case Ttun:
		port->data[0] = namec(dev, Aopen, OREAD, 0);
		port->data[1] = namec(dev2, Aopen, OWRITE, 0);
		break;
	}

	poperror();

	/* committed to binding port */
	b->port[port->id] = port;
	port->bridge = b;
	if(b->nport <= port->id)
		b->nport = port->id+1;

	// assumes kproc always succeeds
	kproc("etherread", etherread, port);	// poperror must be next
	port->ref++;
}

// assumes b is locked
static void
portunbind(Bridge *b, int argc, char *argv[])
{
	int type = 0, i;
	char name[KNAMELEN];
	uint32_t ownhash;
	Port *port = nil;
	static char usage[] = "usage: unbind ether|tunnel addr [ownhash]";

	jehanne_memset(name, 0, KNAMELEN);
	if(argc < 2 || argc > 3)
		error(usage);
	if(jehanne_strcmp(argv[0], "ether") == 0) {
		type = Tether;
		jehanne_strncpy(name, argv[1], KNAMELEN-1);
		name[KNAMELEN-1] = 0;
//		parseaddr(addr, argv[1], Eaddrlen);
	} else if(jehanne_strcmp(argv[0], "tunnel") == 0) {
		type = Ttun;
		jehanne_strncpy(name, argv[1], KNAMELEN-1);
		name[KNAMELEN-1] = 0;
//		parseip(addr, argv[1]);
	} else
		error(usage);
	if(argc == 3)
		ownhash = jehanne_strtoul(argv[2], 0, 0);
	else
		ownhash = 0;
	for(i=0; i<b->nport; i++) {
		port = b->port[i];
		if(port != nil && port->type == type &&
		    jehanne_memcmp(port->name, name, KNAMELEN) == 0)
			break;
	}
	if(i == b->nport)
		error("port not found");
	if(ownhash != 0 && port->ownhash != 0 && ownhash != port->ownhash)
		error("bad owner hash");

	port->closed = 1;
	b->port[i] = nil;	// port is now unbound
	cacheflushport(b, i);

	// try and stop reader
	if(port->readp)
		postnote(port->readp, 1, "unbind", 0);
	portfree(port);
}

// assumes b is locked
static Centry *
cachelookup(Bridge *b, uint8_t d[Eaddrlen])
{
	int i;
	uint h;
	Centry *p;
	int32_t sec;

	// dont cache multicast or broadcast
	if(d[0] & 1)
		return 0;

	h = 0;
	for(i=0; i<Eaddrlen; i++) {
		h *= 7;
		h += d[i];
	}
	h %= CacheHash;
	p = b->cache + h;
	sec = TK2SEC(m->ticks);
	for(i=0; i<CacheLook; i++,p++) {
		if(jehanne_memcmp(d, p->d, Eaddrlen) == 0) {
			p->dst++;
			if(sec >= p->expire) {
				log(b, Logcache, "expired cache entry: %E %d\n",
					d, p->port);
				return nil;
			}
			p->expire = sec + CacheTimeout;
			return p;
		}
	}
	log(b, Logcache, "cache miss: %E\n", d);
	return nil;
}

// assumes b is locked
static void
cacheupdate(Bridge *b, uint8_t d[Eaddrlen], int port)
{
	int i;
	uint h;
	Centry *p, *pp;
	int32_t sec;

	// dont cache multicast or broadcast
	if(d[0] & 1) {
		log(b, Logcache, "bad source address: %E\n", d);
		return;
	}

	h = 0;
	for(i=0; i<Eaddrlen; i++) {
		h *= 7;
		h += d[i];
	}
	h %= CacheHash;
	p = b->cache + h;
	pp = p;
	sec = p->expire;

	// look for oldest entry
	for(i=0; i<CacheLook; i++,p++) {
		if(jehanne_memcmp(p->d, d, Eaddrlen) == 0) {
			p->expire = TK2SEC(m->ticks) + CacheTimeout;
			if(p->port != port) {
				log(b, Logcache, "NIC changed port %d->%d: %E\n",
					p->port, port, d);
				p->port = port;
			}
			p->src++;
			return;
		}
		if(p->expire < sec) {
			sec = p->expire;
			pp = p;
		}
	}
	if(pp->expire != 0)
		log(b, Logcache, "bumping from cache: %E %d\n", pp->d, pp->port);
	pp->expire = TK2SEC(m->ticks) + CacheTimeout;
	jehanne_memmove(pp->d, d, Eaddrlen);
	pp->port = port;
	pp->src = 1;
	pp->dst = 0;
	log(b, Logcache, "adding to cache: %E %d\n", pp->d, pp->port);
}

// assumes b is locked
static void
cacheflushport(Bridge *b, int port)
{
	Centry *ce;
	int i;

	ce = b->cache;
	for(i=0; i<CacheSize; i++,ce++) {
		if(ce->port != port)
			continue;
		jehanne_memset(ce, 0, sizeof(Centry));
	}
}

static char *
cachedump(Bridge *b)
{
	int i, n;
	int32_t sec, off;
	char *buf, *p, *ep;
	Centry *ce;
	char c;

	qlock(&b->ql);
	sec = TK2SEC(m->ticks);
	n = 0;
	for(i=0; i<CacheSize; i++)
		if(b->cache[i].expire != 0)
			n++;
	n *= 51;	// change if print format is changed
	n += 10;	// some slop at the end
	buf = smalloc(n);
	p = buf;
	ep = buf + n;
	ce = b->cache;
	off = seconds() - sec;
	for(i=0; i<CacheSize; i++,ce++) {
		if(ce->expire == 0)
			continue;
		c = (sec < ce->expire)?'v':'e';
		p += jehanne_snprint(p, ep-p, "%E %2d %10ld %10ld %10ld %c\n", ce->d,
			ce->port, ce->src, ce->dst, ce->expire+off, c);
	}
	*p = 0;
	qunlock(&b->ql);
	return buf;
}



// assumes b is locked
static void
ethermultiwrite(Bridge *b, Block *bp, Port *port)
{
	Port *oport;
	Block *bp2;
	Etherpkt *ep;
	int i, mcast;

	if(waserror()) {
		if(bp)
			freeb(bp);
		nexterror();
	}

	ep = (Etherpkt*)bp->rp;
	mcast = ep->d[0] & 1;		/* multicast bit of ethernet address */

	oport = nil;
	for(i=0; i<b->nport; i++) {
		if(i == port->id || b->port[i] == nil)
			continue;
		/*
		 * we need to forward multicast packets for ipv6,
		 * so always do it.
		 */
		if(mcast)
			b->port[i]->outmulti++;
		else
			b->port[i]->outunknown++;

		// delay one so that the last write does not copy
		if(oport != nil) {
			b->copy++;
			bp2 = copyblock(bp, blocklen(bp));
			if(!waserror()) {
				etherwrite(oport, bp2);
				poperror();
			}
		}
		oport = b->port[i];
	}

	// last write free block
	if(oport) {
		bp2 = bp; bp = nil; USED(bp);
		if(!waserror()) {
			etherwrite(oport, bp2);
			poperror();
		}
	} else
		freeb(bp);

	poperror();
}

static void
tcpmsshack(Etherpkt *epkt, int n)
{
	int hl, optlen;
	Iphdr *iphdr;
	Tcphdr *tcphdr;
	uint32_t mss, cksum;
	uint8_t *optr;

	/* ignore non-ipv4 packets */
	if(nhgets(epkt->type) != ETIP4)
		return;
	iphdr = (Iphdr*)(epkt->data);
	n -= ETHERHDRSIZE;
	if(n < IPHDR)
		return;

	/* ignore bad packets */
	if(iphdr->vihl != (IP_VER4|IP_HLEN4)) {
		hl = (iphdr->vihl&0xF)<<2;
		if((iphdr->vihl&0xF0) != IP_VER4 || hl < (IP_HLEN4<<2))
			return;
	} else
		hl = IP_HLEN4<<2;

	/* ignore non-tcp packets */
	if(iphdr->proto != IP_TCPPROTO)
		return;
	n -= hl;
	if(n < sizeof(Tcphdr))
		return;
	tcphdr = (Tcphdr*)((uint8_t*)(iphdr) + hl);
	// MSS can only appear in SYN packet
	if(!(tcphdr->flag[1] & SYN))
		return;
	hl = (tcphdr->flag[0] & 0xf0)>>2;
	if(n < hl)
		return;

	// check for MSS option
	optr = (uint8_t*)tcphdr + sizeof(Tcphdr);
	n = hl - sizeof(Tcphdr);
	for(;;) {
		if(n <= 0 || *optr == EOLOPT)
			return;
		if(*optr == NOOPOPT) {
			n--;
			optr++;
			continue;
		}
		optlen = optr[1];
		if(optlen < 2 || optlen > n)
			return;
		if(*optr == MSSOPT && optlen == MSS_LENGTH)
			break;
		n -= optlen;
		optr += optlen;
	}

	mss = nhgets(optr+2);
	if(mss <= TcpMssMax)
		return;
	// fit checksum
	cksum = nhgets(tcphdr->cksum);
	if(optr-(uint8_t*)tcphdr & 1) {
//		jehanne_print("tcpmsshack: odd alignment!\n");
		// odd alignments are a pain
		cksum += nhgets(optr+1);
		cksum -= (optr[1]<<8)|(TcpMssMax>>8);
		cksum += (cksum>>16);
		cksum &= 0xffff;
		cksum += nhgets(optr+3);
		cksum -= ((TcpMssMax&0xff)<<8)|optr[4];
		cksum += (cksum>>16);
	} else {
		cksum += mss;
		cksum -= TcpMssMax;
		cksum += (cksum>>16);
	}
	hnputs(tcphdr->cksum, cksum);
	hnputs(optr+2, TcpMssMax);
}

/*
 *  process to read from the ethernet
 */
static void
etherread(void *a)
{
	Port *port = a;
	Bridge *b = port->bridge;
	Block *bp, *bp2;
	Etherpkt *ep;
	Centry *ce;
	int32_t md;

	qlock(&b->ql);
	port->readp = up;	/* hide identity under a rock for unbind */

	while(!port->closed){
		// release lock to read - error means it is time to quit
		qunlock(&b->ql);
		if(waserror()) {
			jehanne_print("etherread read error: %s\n", up->errstr);
			qlock(&b->ql);
			break;
		}
		if(0)
			jehanne_print("devbridge: etherread: reading\n");
		bp = devtab[port->data[0]->qid.type]->bread(port->data[0],
			ETHERMAXTU, 0);
		if(0)
			jehanne_print("devbridge: etherread: blocklen = %d\n",
				blocklen(bp));
		poperror();
		qlock(&b->ql);
		if(bp == nil || port->closed)
			break;
		if(waserror()) {
//			jehanne_print("etherread bridge error\n");
			if(bp)
				freeb(bp);
			continue;
		}
		if(blocklen(bp) < ETHERMINTU)
			error("short packet");
		port->in++;

		ep = (Etherpkt*)bp->rp;
		cacheupdate(b, ep->s, port->id);
		if(b->tcpmss)
			tcpmsshack(ep, BLEN(bp));

		/*
		 * delay packets to simulate a slow link
		 */
		if(b->delay0 || b->delayn){
			md = b->delay0 + b->delayn * BLEN(bp);
			if(md > 0)
				microdelay(md);
		}

		if(ep->d[0] & 1) {
			log(b, Logmcast, "multicast: port=%d src=%E dst=%E type=%#.4ux\n",
				port->id, ep->s, ep->d, ep->type[0]<<8|ep->type[1]);
			port->inmulti++;
			bp2 = bp; bp = nil;
			ethermultiwrite(b, bp2, port);
		} else {
			ce = cachelookup(b, ep->d);
			if(ce == nil) {
				b->miss++;
				port->inunknown++;
				bp2 = bp; bp = nil;
				ethermultiwrite(b, bp2, port);
			}else if(ce->port != port->id){
				b->hit++;
				bp2 = bp; bp = nil;
				etherwrite(b->port[ce->port], bp2);
			}
		}

		poperror();
		if(bp)
			freeb(bp);
	}
//	jehanne_print("etherread: trying to exit\n");
	port->readp = nil;
	portfree(port);
	qunlock(&b->ql);
	pexit("hangup", 1);
}

static int
fragment(Etherpkt *epkt, int n)
{
	Iphdr *iphdr;

	if(n <= TunnelMtu)
		return 0;

	/* ignore non-ipv4 packets */
	if(nhgets(epkt->type) != ETIP4)
		return 0;
	iphdr = (Iphdr*)(epkt->data);
	n -= ETHERHDRSIZE;
	/*
	 * ignore: IP runt packets, bad packets (I don't handle IP
	 * options for the moment), packets with don't-fragment set,
	 * and short blocks.
	 */
	if(n < IPHDR || iphdr->vihl != (IP_VER4|IP_HLEN4) ||
	    iphdr->frag[0] & (IP_DF>>8) || nhgets(iphdr->length) > n)
		return 0;

	return 1;
}


static void
etherwrite(Port *port, Block *bp)
{
	Iphdr *eh, *feh;
	Etherpkt *epkt;
	int n, lid, len, seglen, chunk, dlen, blklen, offset, mf;
	Block *xp, *nb;
	uint16_t fragoff, frag;

	port->out++;
	epkt = (Etherpkt*)bp->rp;
	n = blocklen(bp);
	if(port->type != Ttun || !fragment(epkt, n)) {
		devtab[port->data[1]->qid.type]->bwrite(port->data[1], bp, 0);
		return;
	}
	port->outfrag++;
	if(waserror()){
		freeblist(bp);
		nexterror();
	}

	seglen = (TunnelMtu - ETHERHDRSIZE - IPHDR) & ~7;
	eh = (Iphdr*)(epkt->data);
	len = nhgets(eh->length);
	frag = nhgets(eh->frag);
	mf = frag & IP_MF;
	frag <<= 3;
	dlen = len - IPHDR;
	xp = bp;
	lid = nhgets(eh->id);
	offset = ETHERHDRSIZE+IPHDR;
	while(xp != nil && offset && offset >= BLEN(xp)) {
		offset -= BLEN(xp);
		xp = xp->next;
	}
	xp->rp += offset;

	if(0)
		jehanne_print("seglen=%d, dlen=%d, mf=%x, frag=%d\n",
			seglen, dlen, mf, frag);
	for(fragoff = 0; fragoff < dlen; fragoff += seglen) {
		nb = allocb(ETHERHDRSIZE+IPHDR+seglen);

		feh = (Iphdr*)(nb->wp+ETHERHDRSIZE);

		jehanne_memmove(nb->wp, epkt, ETHERHDRSIZE+IPHDR);
		nb->wp += ETHERHDRSIZE+IPHDR;

		if((fragoff + seglen) >= dlen) {
			seglen = dlen - fragoff;
			hnputs(feh->frag, (frag+fragoff)>>3 | mf);
		}
		else
			hnputs(feh->frag, (frag+fragoff>>3) | IP_MF);

		hnputs(feh->length, seglen + IPHDR);
		hnputs(feh->id, lid);

		/* Copy up the data area */
		chunk = seglen;
		while(chunk) {
			blklen = chunk;
			if(BLEN(xp) < chunk)
				blklen = BLEN(xp);
			jehanne_memmove(nb->wp, xp->rp, blklen);
			nb->wp += blklen;
			xp->rp += blklen;
			chunk -= blklen;
			if(xp->rp == xp->wp)
				xp = xp->next;
		}

		feh->cksum[0] = 0;
		feh->cksum[1] = 0;
		hnputs(feh->cksum, ipcsum(&feh->vihl));

		/* don't generate small packets */
		if(BLEN(nb) < ETHERMINTU)
			nb->wp = nb->rp + ETHERMINTU;
		devtab[port->data[1]->qid.type]->bwrite(port->data[1], nb, 0);
	}
	poperror();
	freeblist(bp);
}

// hold b lock
static void
portfree(Port *port)
{
	port->ref--;
	if(port->ref < 0)
		panic("portfree: bad ref");
	if(port->ref > 0)
		return;

	if(port->data[0])
		cclose(port->data[0]);
	if(port->data[1])
		cclose(port->data[1]);
	jehanne_memset(port, 0, sizeof(Port));
	jehanne_free(port);
}

Dev bridgedevtab = {
	'B',
	"bridge",

	devreset,
	bridgeinit,
	devshutdown,
	bridgeattach,
	bridgewalk,
	bridgestat,
	bridgeopen,
	devcreate,
	bridgeclose,
	bridgeread,
	devbread,
	bridgewrite,
	devbwrite,
	devremove,
	devwstat,
};
