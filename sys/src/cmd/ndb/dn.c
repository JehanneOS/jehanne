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
#include <u.h>
#include <lib9.h>
#include <ip.h>
#include <chartypes.h>
#include "dns.h"

/*
 *  this comment used to say `our target is 4000 names cached, this should
 *  be larger on large servers'.  dns at Bell Labs starts off with
 *  about 1780 names.
 *
 * aging seems to corrupt the cache, so raise the trigger from 4000 until we
 * figure it out.
 */
enum {
	/* these settings will trigger frequent aging */
	Deftarget	= 4000,
	Minage		=  5*Min,
	Defagefreq	= 15*Min,	/* age names this often (seconds) */
};

/*
 *  Hash table for domain names.  The hash is based only on the
 *  first element of the domain name.
 */
DN *ht[HTLEN];

static struct {
	Lock;
	uint32_t	names;		/* names allocated */
	uint32_t	oldest;		/* longest we'll leave a name around */
	int	active;
	int	mutex;
	uint16_t	id;		/* same size as in packet */
} dnvars;

/* names of RR types */
char *rrtname[] =
{
[Ta]		"ip",
[Tns]		"ns",
[Tmd]		"md",
[Tmf]		"mf",
[Tcname]	"cname",
[Tsoa]		"soa",
[Tmb]		"mb",
[Tmg]		"mg",
[Tmr]		"mr",
[Tnull]		"null",
[Twks]		"wks",
[Tptr]		"ptr",
[Thinfo]	"hinfo",
[Tminfo]	"minfo",
[Tmx]		"mx",
[Ttxt]		"txt",
[Trp]		"rp",
[Tafsdb]	"afsdb",
[Tx25]		"x.25",
[Tisdn]		"isdn",
[Trt]		"rt",
[Tnsap]		"nsap",
[Tnsapptr]	"nsap-ptr",
[Tsig]		"sig",
[Tkey]		"key",
[Tpx]		"px",
[Tgpos]		"gpos",
[Taaaa]		"ipv6",
[Tloc]		"loc",
[Tnxt]		"nxt",
[Teid]		"eid",
[Tnimloc]	"nimrod",
[Tsrv]		"srv",
[Tatma]		"atma",
[Tnaptr]	"naptr",
[Tkx]		"kx",
[Tcert]		"cert",
[Ta6]		"a6",
[Tdname]	"dname",
[Tsink]		"sink",
[Topt]		"opt",
[Tapl]		"apl",
[Tds]		"ds",
[Tsshfp]	"sshfp",
[Tipseckey]	"ipseckey",
[Trrsig]	"rrsig",
[Tnsec]		"nsec",
[Tdnskey]	"dnskey",
[Tspf]		"spf",
[Tuinfo]	"uinfo",
[Tuid]		"uid",
[Tgid]		"gid",
[Tunspec]	"unspec",
[Ttkey]		"tkey",
[Ttsig]		"tsig",
[Tixfr]		"ixfr",
[Taxfr]		"axfr",
[Tmailb]	"mailb",
[Tmaila]	"maila",
[Tall]		"all",
		0,
};

/* names of response codes */
char *rname[Rmask+1] =
{
[Rok]			"ok",
[Rformat]		"format error",
[Rserver]		"server failure",
[Rname]			"bad name",
[Runimplimented]	"unimplemented",
[Rrefused]		"we don't like you",
[Ryxdomain]		"name should not exist",
[Ryxrrset]		"rr set should not exist",
[Rnxrrset]		"rr set should exist",
[Rnotauth]		"not authorative",
[Rnotzone]		"not in zone",
[Rbadvers]		"bad opt version",
/* [Rbadsig]		"bad signature", */
[Rbadkey]		"bad key",
[Rbadtime]		"bad signature time",
[Rbadmode]		"bad mode",
[Rbadname]		"duplicate key name",
[Rbadalg]		"bad algorithm",
};
unsigned nrname = nelem(rname);

/* names of op codes */
char *opname[] =
{
[Oquery]	"query",
[Oinverse]	"inverse query (retired)",
[Ostatus]	"status",
[Oupdate]	"update",
};

uint32_t target = Deftarget;
Lock	dnlock;

static uint32_t agefreq = Defagefreq;

static int rrequiv(RR *r1, RR *r2);
static int sencodefmt(Fmt*);

static void
ding(void* _1, char *msg)
{
	if(strstr(msg, "alarm") != nil) {
		stats.alarms++;
		sys_noted(NCONT);		/* resume with system call error */
	} else
		sys_noted(NDFLT);		/* die */
}

void
dninit(void)
{
	fmtinstall('E', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('R', rrfmt);
	fmtinstall('Q', rravfmt);
	fmtinstall('H', sencodefmt);

	dnvars.oldest = maxage;
	dnvars.names = 0;
	dnvars.id = truerand();	/* don't start with same id every time */

	sys_notify(ding);
}

/*
 *  hash for a domain name
 */
static uint32_t
dnhash(char *name)
{
	uint32_t hash;
	uint8_t *val = (uint8_t*)name;

	for(hash = 0; *val; val++)
		hash = hash*13 + tolower(*val)-'a';
	return hash % HTLEN;
}

/*
 *  lookup a symbol.  if enter is not zero and the name is
 *  not found, create it.
 */
DN*
dnlookup(char *name, int class, int enter)
{
	DN **l;
	DN *dp;

	l = &ht[dnhash(name)];
	jehanne_lock(&dnlock);
	for(dp = *l; dp; dp = dp->next) {
		assert(dp->magic == DNmagic);
		if(dp->class == class && cistrcmp(dp->name, name) == 0){
			dp->referenced = now;
			jehanne_unlock(&dnlock);
			return dp;
		}
		l = &dp->next;
	}

	if(!enter){
		jehanne_unlock(&dnlock);
		return 0;
	}
	dnvars.names++;
	dp = emalloc(sizeof(*dp));
	dp->magic = DNmagic;
	dp->name = estrdup(name);
	dp->class = class;
	dp->rr = nil;
	dp->referenced = now;
	/* add new DN to tail of the hash list.  *l points to last next ptr. */
	dp->next = nil;
	*l = dp;
	jehanne_unlock(&dnlock);

	return dp;
}

DN*
idnlookup(char *name, int class, int enter)
{
	char dom[Domlen];

	if(utf2idn(name, dom, sizeof dom) != nil)
		name = dom;
	return dnlookup(name, class, enter);
}

static int
rrsame(RR *rr1, RR *rr2)
{
	return rr1 == rr2 || rr2 && rrequiv(rr1, rr2) &&
		rr1->db == rr2->db && rr1->auth == rr2->auth;
}

static int
rronlist(RR *rp, RR *lp)
{
	for(; lp; lp = lp->next)
		if (rrsame(lp, rp))
			return 1;
	return 0;
}

/*
 * dump the stats
 */
void
dnstats(char *file)
{
	int i, fd;

	fd = ocreate(file, OWRITE, 0666);
	if(fd < 0)
		return;

	qlock(&stats);
	fprint(fd, "# system %s\n", sysname());
	fprint(fd, "# slave procs high-water mark\t%lud\n", stats.slavehiwat);
	fprint(fd, "# queries received by 9p\t%lud\n", stats.qrecvd9p);
	fprint(fd, "# queries received by udp\t%lud\n", stats.qrecvdudp);
	fprint(fd, "# queries answered from memory\t%lud\n", stats.answinmem);
	fprint(fd, "# queries sent by udp\t%lud\n", stats.qsent);
	for (i = 0; i < nelem(stats.under10ths); i++)
		if (stats.under10ths[i] || i == nelem(stats.under10ths) - 1)
			fprint(fd, "# responses arriving within %.1f s.\t%lud\n",
				(double)(i+1)/10, stats.under10ths[i]);
	fprint(fd, "\n# queries sent & timed-out\t%lud\n", stats.tmout);
	fprint(fd, "# cname queries timed-out\t%lud\n", stats.tmoutcname);
	fprint(fd, "# ipv6  queries timed-out\t%lud\n", stats.tmoutv6);
	fprint(fd, "\n# negative answers received\t%lud\n", stats.negans);
	fprint(fd, "# negative answers w Rserver set\t%lud\n", stats.negserver);
	fprint(fd, "# negative answers w bad delegation\t%lud\n",
		stats.negbaddeleg);
	fprint(fd, "# negative answers w bad delegation & no answers\t%lud\n",
		stats.negbdnoans);
	fprint(fd, "# negative answers w no Rname set\t%lud\n", stats.negnorname);
	fprint(fd, "# negative answers cached\t%lud\n", stats.negcached);
	qunlock(&stats);

	jehanne_lock(&dnlock);
	fprint(fd, "\n# domain names %lud target %lud\n", dnvars.names, target);
	jehanne_unlock(&dnlock);
	sys_close(fd);
}

/*
 *  dump the cache
 */
void
dndump(char *file)
{
	int i, fd;
	DN *dp;
	RR *rp;

	fd = ocreate(file, OWRITE, 0666);
	if(fd < 0)
		return;

	jehanne_lock(&dnlock);
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			fprint(fd, "%s\n", dp->name);
			for(rp = dp->rr; rp; rp = rp->next) {
				fprint(fd, "\t%R %c%c %ld/%lud\n",
					rp, rp->auth? 'A': 'U',
					rp->db? 'D': 'N', (int32_t)(rp->expire - now), rp->ttl);
				if (rronlist(rp, rp->next))
					fprint(fd, "*** duplicate:\n");
			}
		}
	jehanne_unlock(&dnlock);
	sys_close(fd);
}

/*
 *  purge all records
 */
void
dnpurge(void)
{
	DN *dp;
	RR *rp, *srp;
	int i;

	jehanne_lock(&dnlock);

	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			srp = rp = dp->rr;
			dp->rr = nil;
			for(; rp != nil; rp = rp->next)
				rp->cached = 0;
			rrfreelist(srp);
		}

	jehanne_unlock(&dnlock);
}

/*
 *  delete head of *l and free the old head.
 *  call with dnlock held.
 */
static void
rrdelhead(RR **l)
{
	RR *rp;

	if (canlock(&dnlock))
		abort();	/* rrdelhead called with dnlock not held */
	rp = *l;
	if(rp == nil)
		return;
	*l = rp->next;		/* unlink head */
	rp->cached = 0;		/* avoid blowing an assertion in rrfree */
	rrfree(rp);
}

/*
 *  check the age of resource records, free any that have timed out.
 *  call with dnlock held.
 */
void
dnage(DN *dp)
{
	RR **l, *rp;
	uint32_t diff;

	if (canlock(&dnlock))
		abort();	/* dnage called with dnlock not held */
	diff = now - dp->referenced;
	if(diff < Reserved || dp->mark != 0)
		return;

	l = &dp->rr;
	while ((rp = *l) != nil){
		assert(rp->magic == RRmagic && rp->cached);
		if(!rp->db && ((int32_t)(rp->expire - now) <= 0 || diff > dnvars.oldest))
			rrdelhead(l); /* rp == *l before; *l == rp->next after */
		else
			l = &rp->next;
	}
}

#define MARK(dp)	{ if (dp) (dp)->mark |= 2; }

/* mark a domain name and those in its RRs as never to be aged */
void
dnagenever(DN *dp)
{
	RR *rp;

	jehanne_lock(&dnlock);

	/* mark all referenced domain names */
	MARK(dp);
	for(rp = dp->rr; rp; rp = rp->next){
		MARK(rp->owner);
		if(rp->negative){
			MARK(rp->negsoaowner);
			continue;
		}
		switch(rp->type){
		case Thinfo:
			MARK(rp->cpu);
			MARK(rp->os);
			break;
		case Ttxt:
			break;
		case Tcname:
		case Tmb:
		case Tmd:
		case Tmf:
		case Tns:
		case Tmx:
		case Tsrv:
			MARK(rp->host);
			break;
		case Tmg:
		case Tmr:
			MARK(rp->mb);
			break;
		case Tminfo:
			MARK(rp->rmb);
			MARK(rp->mb);
			break;
		case Trp:
			MARK(rp->rmb);
			MARK(rp->rp);
			break;
		case Ta:
		case Taaaa:
			MARK(rp->ip);
			break;
		case Tptr:
			MARK(rp->ptr);
			break;
		case Tsoa:
			MARK(rp->host);
			MARK(rp->rmb);
			break;
		case Tsig:
			MARK(rp->sig->signer);
			break;
		}
	}

	jehanne_unlock(&dnlock);
}

#define REF(dp)	{ if (dp) (dp)->mark |= 1; }

/*
 *  periodicly sweep for old records and remove unreferenced domain names
 *
 *  only called when all other threads are locked out
 */
void
dnageall(int doit)
{
	DN *dp, **l;
	int i;
	RR *rp;
	static uint32_t nextage;

	if(dnvars.names < target || ((int32_t)(nextage - now) > 0 && !doit)){
		dnvars.oldest = maxage;
		return;
	}

	if(dnvars.names >= target) {
		dnslog("more names (%lud) than target (%lud)", dnvars.names,
			target);
		dnvars.oldest /= 2;
		if (dnvars.oldest < Minage)
			dnvars.oldest = Minage;		/* don't be silly */
	}
	if (agefreq > dnvars.oldest / 2)
		nextage = now + dnvars.oldest / 2;
	else
		nextage = now + (uint32_t)agefreq;

	jehanne_lock(&dnlock);

	/* time out all old entries (and set refs to 0) */
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			dp->mark &= ~1;
			dnage(dp);
		}

	/* mark all referenced domain names */
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next)
			for(rp = dp->rr; rp; rp = rp->next){
				REF(rp->owner);
				if(rp->negative){
					REF(rp->negsoaowner);
					continue;
				}
				switch(rp->type){
				case Thinfo:
					REF(rp->cpu);
					REF(rp->os);
					break;
				case Ttxt:
					break;
				case Tcname:
				case Tmb:
				case Tmd:
				case Tmf:
				case Tns:
				case Tmx:
				case Tsrv:
					REF(rp->host);
					break;
				case Tmg:
				case Tmr:
					REF(rp->mb);
					break;
				case Tminfo:
					REF(rp->rmb);
					REF(rp->mb);
					break;
				case Trp:
					REF(rp->rmb);
					REF(rp->rp);
					break;
				case Ta:
				case Taaaa:
					REF(rp->ip);
					break;
				case Tptr:
					REF(rp->ptr);
					break;
				case Tsoa:
					REF(rp->host);
					REF(rp->rmb);
					break;
				case Tsig:
					REF(rp->sig->signer);
					break;
				}
			}

	/* sweep and remove unreferenced domain names */
	for(i = 0; i < HTLEN; i++){
		l = &ht[i];
		for(dp = *l; dp; dp = *l){
			if(dp->rr == nil && dp->mark == 0){
				assert(dp->magic == DNmagic);
				*l = dp->next;

				free(dp->name);
				memset(dp, 0, sizeof *dp); /* cause trouble */
				dp->magic = ~DNmagic;
				free(dp);

				dnvars.names--;
				continue;
			}
			l = &dp->next;
		}
	}

	jehanne_unlock(&dnlock);
}

/*
 *  timeout all database records (used when rereading db)
 */
void
dnagedb(void)
{
	DN *dp;
	int i;
	RR *rp;

	jehanne_lock(&dnlock);

	/* time out all database entries */
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next) {
			dp->mark = 0;
			for(rp = dp->rr; rp; rp = rp->next)
				if(rp->db)
					rp->expire = 0;
		}

	jehanne_unlock(&dnlock);
}

/*
 *  mark all local db records about my area as authoritative,
 *  delete timed out ones
 */
void
dnauthdb(void)
{
	int i;
	uint32_t minttl;
	Area *area;
	DN *dp;
	RR *rp, **l;

	jehanne_lock(&dnlock);

	/* time out all database entries */
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			area = inmyarea(dp->name);
			l = &dp->rr;
			for(rp = *l; rp; rp = *l){
				if(rp->db){
					if(rp->expire == 0){
						rrdelhead(l);
						continue;
					}
					if(area){
						minttl = area->soarr->soa->minttl;
						if(rp->ttl < minttl)
							rp->ttl = minttl;
						rp->auth = 1;
					}
				}
				l = &rp->next;
			}
		}

	jehanne_unlock(&dnlock);
}

/*
 *  keep track of other processes to know if we can
 *  garbage collect.  block while garbage collecting.
 */
int
getactivity(Request *req, int recursive)
{
	int rv;

	if(traceactivity)
		dnslog("get: %d active by pid %d from %p",
			dnvars.active, getpid(), getcallerpc());
	jehanne_lock(&dnvars);
	/*
	 * can't block here if we're already holding one
	 * of the dnvars.active (recursive).  will deadlock.
	 */
	while(!recursive && dnvars.mutex){
		jehanne_unlock(&dnvars);
		sleep(100);			/* tune; was 200 */
		jehanne_lock(&dnvars);
	}
	rv = ++dnvars.active;
	now = time(nil);
	nowns = nsec();
	req->id = ++dnvars.id;
	req->aux = nil;
	jehanne_unlock(&dnvars);

	return rv;
}
void
putactivity(int recursive)
{
	if(traceactivity)
		dnslog("put: %d active by pid %d",
			dnvars.active, getpid());
	jehanne_lock(&dnvars);
	dnvars.active--;
	assert(dnvars.active >= 0); /* "dnvars.active %d", dnvars.active */

	/*
	 *  clean out old entries and check for new db periodicly
	 *  can't block here if being called to let go a "recursive" lock
	 *  or we'll deadlock waiting for ourselves to give up the dnvars.active.
	 */
	if (recursive || dnvars.mutex ||
	    (needrefresh == 0 && dnvars.active > 0)){
		jehanne_unlock(&dnvars);
		return;
	}

	/* wait till we're alone */
	dnvars.mutex = 1;
	while(dnvars.active > 0){
		jehanne_unlock(&dnvars);
		sleep(100);		/* tune; was 100 */
		jehanne_lock(&dnvars);
	}
	jehanne_unlock(&dnvars);

	db2cache(needrefresh);

	dnageall(0);

	/* let others back in */
	needrefresh = 0;
	dnvars.mutex = 0;
}

int
rrlistlen(RR *rp)
{
	int n;

	n = 0;
	for(; rp; rp = rp->next)
		++n;
	return n;
}

/*
 *  Attach a single resource record to a domain name (new->owner).
 *	- Avoid duplicates with already present RR's
 *	- Chain all RR's of the same type adjacent to one another
 *	- chain authoritative RR's ahead of non-authoritative ones
 *	- remove any expired RR's
 *  If new is a stale duplicate, rrfree it.
 *  Must be called with dnlock held.
 */
static void
rrattach1(RR *new, int auth)
{
	RR **l;
	RR *rp;
	DN *dp;
	uint32_t ttl;

	assert(new->magic == RRmagic && !new->cached);

	dp = new->owner;
	assert(dp != nil && dp->magic == DNmagic);
	new->auth |= auth;
	new->next = 0;

	/*
	 * try not to let responses expire before we
	 * can use them to complete this query, by extending
	 * past (or nearly past) expiration time.
	 */
	if(new->db)
		ttl = Year;
	else
		ttl = new->ttl;
	if(ttl <= Min)
		ttl = 10*Min;
	new->expire = now + ttl;

	/*
	 *  find first rr of the right type
	 */
	l = &dp->rr;
	for(rp = *l; rp; rp = *l){
		assert(rp->magic == RRmagic && rp->cached);
		if(rp->type == new->type)
			break;
		l = &rp->next;
	}

	/*
	 *  negative entries replace positive entries
	 *  positive entries replace negative entries
	 *  newer entries replace older entries with the same fields
	 *
	 *  look farther ahead than just the next entry when looking
	 *  for duplicates; RRs of a given type can have different rdata
	 *  fields (e.g. multiple NS servers).
	 */
	while ((rp = *l) != nil){
		assert(rp->magic == RRmagic && rp->cached);
		if(rp->type != new->type)
			break;

		if(rp->db == new->db && rp->auth == new->auth){
			/* negative drives out positive and vice versa */
			if(rp->negative != new->negative) {
				/* rp == *l before; *l == rp->next after */
				rrdelhead(l);
				continue;
			}
			/* all things equal, pick the newer one */
			else if(rp->arg0 == new->arg0 && rp->arg1 == new->arg1){
				/* old drives out new */
				if((int32_t)(rp->expire - new->expire) > 0) {
					rrfree(new);
					return;
				}
				/* rp == *l before; *l == rp->next after */
				rrdelhead(l);
				continue;
			}
			/*
			 *  Hack for pointer records.  This makes sure
			 *  the ordering in the list reflects the ordering
			 *  received or read from the database
			 */
			else if(rp->type == Tptr &&
			    !rp->negative && !new->negative &&
			    rp->ptr->ordinal > new->ptr->ordinal)
				break;
		}
		l = &rp->next;
	}

	if (rronlist(new, rp)) {
		/* should not happen; duplicates were processed above */
		dnslog("adding duplicate %R to list of %R; aborting", new, rp);
		abort();
	}
	/*
	 *  add to chain
	 */
	new->cached = 1;
	new->next = rp;
	*l = new;
}

/*
 *  Attach a list of resource records to a domain name.
 *  May rrfree any stale duplicate RRs; dismembers the list.
 *  Upon return, every RR in the list will have been rrfree-d
 *  or attached to its domain name.
 *  See rrattach1 for properties preserved.
 */
void
rrattach(RR *rp, int auth)
{
	RR *next;
	DN *dp;

	jehanne_lock(&dnlock);
	for(; rp; rp = next){
		next = rp->next;
		rp->next = nil;
		dp = rp->owner;
		/* avoid any outside spoofing */
		if(cfg.cachedb && !rp->db && inmyarea(dp->name))
			rrfree(rp);
		else
			rrattach1(rp, auth);
	}
	jehanne_unlock(&dnlock);
}

RR**
rrcopy(RR *rp, RR **last)
{
	RR *nrp;
	SOA *soa;
	Srv *srv;
	Key *key;
	Cert *cert;
	Sig *sig;
	Null *null;
	Txt *t, *nt, **l;

	assert(rp->magic == RRmagic);
	nrp = rralloc(rp->type);
	switch(rp->type){
	case Tsoa:
		soa = nrp->soa;
		*nrp = *rp;
		nrp->soa = soa;
		*soa = *rp->soa;
		soa->slaves = copyserverlist(rp->soa->slaves);
		break;
	case Tsrv:
		srv = nrp->srv;
		*nrp = *rp;
		nrp->srv = srv;
		*srv = *rp->srv;
		break;
	case Tkey:
		key = nrp->key;
		*nrp = *rp;
		nrp->key = key;
		*key = *rp->key;
		key->data = emalloc(key->dlen);
		memmove(key->data, rp->key->data, rp->key->dlen);
		break;
	case Tcert:
		cert = nrp->cert;
		*nrp = *rp;
		nrp->cert = cert;
		*cert = *rp->cert;
		cert->data = emalloc(cert->dlen);
		memmove(cert->data, rp->cert->data, rp->cert->dlen);
		break;
	case Tsig:
		sig = nrp->sig;
		*nrp = *rp;
		nrp->sig = sig;
		*sig = *rp->sig;
		sig->data = emalloc(sig->dlen);
		memmove(sig->data, rp->sig->data, rp->sig->dlen);
		break;
	case Tnull:
		null = nrp->null;
		*nrp = *rp;
		nrp->null = null;
		*null = *rp->null;
		null->data = emalloc(null->dlen);
		memmove(null->data, rp->null->data, rp->null->dlen);
		break;
	case Ttxt:
		*nrp = *rp;
		l = &nrp->txt;
		*l = nil;
		for(t = rp->txt; t != nil; t = t->next){
			nt = emalloc(sizeof(*nt));
			nt->p = estrdup(t->p);
			nt->next = nil;
			*l = nt;
			l = &nt->next;
		}
		break;
	default:
		*nrp = *rp;
		break;
	}
	nrp->pc = getcallerpc();
	setmalloctag(nrp, nrp->pc);
	nrp->cached = 0;
	nrp->next = nil;
	*last = nrp;
	return &nrp->next;
}

/*
 *  lookup a resource record of a particular type and
 *  class attached to a domain name.  Return copies.
 *
 *  Priority ordering is:
 *	db authoritative
 *	not timed out network authoritative
 *	not timed out network unauthoritative
 *	unauthoritative db
 *
 *  if flag NOneg is set, don't return negative cached entries.
 *  return nothing instead.
 */
RR*
rrlookup(DN *dp, int type, int flag)
{
	RR *rp, *first, **last;

	assert(dp->magic == DNmagic);

	first = nil;
	last = &first;
	jehanne_lock(&dnlock);

	/* try for an authoritative db entry */
	for(rp = dp->rr; rp; rp = rp->next){
		assert(rp->magic == RRmagic && rp->cached);
		if(rp->db)
		if(rp->auth)
		if(tsame(type, rp->type))
			last = rrcopy(rp, last);
	}
	if(first)
		goto out;

	/* try for a living authoritative network entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(!rp->db)
		if(rp->auth)
		if((int32_t)(rp->expire - now) > 0)
 		if(tsame(type, rp->type)){
			if(flag == NOneg && rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}
	}
	if(first)
		goto out;

	/* try for a living unauthoritative network entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(!rp->db)
		if((int32_t)(rp->expire - now) > 0)
		if(tsame(type, rp->type)){
			if(flag == NOneg && rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}
	}
	if(first)
		goto out;

	/* try for an unauthoritative db entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(rp->db)
		if(tsame(type, rp->type))
			last = rrcopy(rp, last);
	}
	if(first)
		goto out;

	/* otherwise, settle for anything we got (except for negative caches) */
	for(rp = dp->rr; rp; rp = rp->next)
		if(tsame(type, rp->type)){
			if(rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}

out:
	jehanne_unlock(&dnlock);
	unique(first);
	return first;
}

/*
 *  convert an ascii RR type name to its integer representation
 */
int
rrtype(char *atype)
{
	int i;

	for(i = 0; i <= Tall; i++)
		if(rrtname[i] && strcmp(rrtname[i], atype) == 0)
			return i;

	/* make any a synonym for all */
	if(strcmp(atype, "any") == 0)
		return Tall;
	else if(isascii(atype[0]) && isdigit(atype[0]))
		return atoi(atype);
	else
		return -1;
}

/*
 *  return 0 if not a supported rr type
 */
int
rrsupported(int type)
{
	if(type < 0 || type >Tall)
		return 0;
	return rrtname[type] != nil;
}

/*
 *  compare 2 types
 */
int
tsame(int t1, int t2)
{
	return t1 == t2 || t1 == Tall;
}

/*
 *  Add resource records to a list.
 */
RR*
rrcat(RR **start, RR *rp)
{
	RR *olp, *nlp;
	RR **last;

	/* check for duplicates */
	for (olp = *start; 0 && olp; olp = olp->next)
		for (nlp = rp; nlp; nlp = nlp->next)
			if (rrsame(nlp, olp))
				dnslog("rrcat: duplicate RR: %R", nlp);
	USED(olp);

	last = start;
	while(*last != nil)
		last = &(*last)->next;

	*last = rp;
	return *start;
}

RR*
rrremfilter(RR **l, int (*filter)(RR*, void*), void *arg)
{
	RR *first, *rp;
	RR **nl;

	first = nil;
	nl = &first;
	while(*l != nil){
		rp = *l;
		if((*filter)(rp, arg)){
			*l = rp->next;
			*nl = rp;
			nl = &rp->next;
			*nl = nil;
		} else
			l = &(*l)->next;
	}

	return first;
}

static int
filterneg(RR *rp, void* _1)
{
	return rp->negative;
}
static int
filtertype(RR *rp, void *arg)
{
	return rp->type == *((int*)arg);
}
static int
filterowner(RR *rp, void *arg)
{
	return rp->owner == (DN*)arg;
}

/*
 *  remove negative cache rr's from an rr list
 */
RR*
rrremneg(RR **l)
{
	return rrremfilter(l, filterneg, nil);
}

/*
 *  remove rr's of a particular type from an rr list
 */
RR*
rrremtype(RR **l, int type)
{
	return rrremfilter(l, filtertype, &type);
}

/*
 *  remove rr's of a particular owner from an rr list
 */
RR*
rrremowner(RR **l, DN *owner)
{
	return rrremfilter(l, filterowner, owner);
}

static char *
dnname(DN *dn)
{
	return dn? dn->name: "<null>";
}

static char *
idnname(DN *dn, char *buf, int nbuf)
{
	char *name;

	name = dnname(dn);
	if(idn2utf(name, buf, nbuf) != nil)
		return buf;
	return name;
}

/*
 *  print conversion for rr records
 */
int
rrfmt(Fmt *f)
{
	int rv;
	char *strp;
	char buf[Domlen];
	Fmt fstr;
	RR *rp;
	Server *s;
	SOA *soa;
	Srv *srv;
	Txt *t;

	fmtstrinit(&fstr);

	rp = va_arg(f->args, RR*);
	if(rp == nil){
		fmtprint(&fstr, "<null>");
		goto out;
	}

	fmtprint(&fstr, "%s %s", dnname(rp->owner),
		rrname(rp->type, buf, sizeof buf));

	if(rp->negative){
		fmtprint(&fstr, "\tnegative - rcode %d", rp->negrcode);
		goto out;
	}

	switch(rp->type){
	case Thinfo:
		fmtprint(&fstr, "\t%s %s", dnname(rp->cpu), dnname(rp->os));
		break;
	case Tcname:
	case Tmb:
	case Tmd:
	case Tmf:
	case Tns:
		fmtprint(&fstr, "\t%s", dnname(rp->host));
		break;
	case Tmg:
	case Tmr:
		fmtprint(&fstr, "\t%s", dnname(rp->mb));
		break;
	case Tminfo:
		fmtprint(&fstr, "\t%s %s", dnname(rp->mb), dnname(rp->rmb));
		break;
	case Tmx:
		fmtprint(&fstr, "\t%lud %s", rp->pref, dnname(rp->host));
		break;
	case Ta:
	case Taaaa:
		fmtprint(&fstr, "\t%s", dnname(rp->ip));
		break;
	case Tptr:
		fmtprint(&fstr, "\t%s", dnname(rp->ptr));
		break;
	case Tsoa:
		soa = rp->soa;
		fmtprint(&fstr, "\t%s %s %lud %lud %lud %lud %lud",
			dnname(rp->host), dnname(rp->rmb),
			(soa? soa->serial: 0),
			(soa? soa->refresh: 0), (soa? soa->retry: 0),
			(soa? soa->expire: 0), (soa? soa->minttl: 0));
		if (soa)
			for(s = soa->slaves; s != nil; s = s->next)
				fmtprint(&fstr, " %s", s->name);
		break;
	case Tsrv:
		srv = rp->srv;
		fmtprint(&fstr, "\t%ud %ud %ud %s",
			(srv? srv->pri: 0), (srv? srv->weight: 0),
			rp->port, dnname(rp->host));
		break;
	case Tnull:
		if (rp->null == nil)
			fmtprint(&fstr, "\t<null>");
		else
			fmtprint(&fstr, "\t%.*H", rp->null->dlen,
				rp->null->data);
		break;
	case Ttxt:
		fmtprint(&fstr, "\t");
		for(t = rp->txt; t != nil; t = t->next)
			fmtprint(&fstr, "%s", t->p);
		break;
	case Trp:
		fmtprint(&fstr, "\t%s %s", dnname(rp->rmb), dnname(rp->rp));
		break;
	case Tkey:
		if (rp->key == nil)
			fmtprint(&fstr, "\t<null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d", rp->key->flags,
				rp->key->proto, rp->key->alg);
		break;
	case Tsig:
		if (rp->sig == nil)
			fmtprint(&fstr,
		   "\t<null> <null> <null> <null> <null> <null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d %lud %lud %lud %d %s",
				rp->sig->type, rp->sig->alg, rp->sig->labels,
				rp->sig->ttl, rp->sig->exp, rp->sig->incep,
				rp->sig->tag, dnname(rp->sig->signer));
		break;
	case Tcert:
		if (rp->cert == nil)
			fmtprint(&fstr, "\t<null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d",
				rp->cert->type, rp->cert->tag, rp->cert->alg);
		break;
	}
out:
	strp = fmtstrflush(&fstr);
	rv = fmtstrcpy(f, strp);
	free(strp);
	return rv;
}

/*
 *  print conversion for rr records in attribute value form
 */
int
rravfmt(Fmt *f)
{
	int rv, quote;
	char buf[Domlen], *strp;
	Fmt fstr;
	RR *rp;
	Server *s;
	SOA *soa;
	Srv *srv;
	Txt *t;

	fmtstrinit(&fstr);

	rp = va_arg(f->args, RR*);
	if(rp == nil){
		fmtprint(&fstr, "<null>");
		goto out;
	}

	if(rp->type == Tptr)
		fmtprint(&fstr, "ptr=%s", dnname(rp->owner));
	else
		fmtprint(&fstr, "dom=%s", idnname(rp->owner, buf, sizeof(buf)));

	switch(rp->type){
	case Thinfo:
		fmtprint(&fstr, " cpu=%s os=%s",
			idnname(rp->cpu, buf, sizeof(buf)),
			idnname(rp->os, buf, sizeof(buf)));
		break;
	case Tcname:
		fmtprint(&fstr, " cname=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tmb:
	case Tmd:
	case Tmf:
		fmtprint(&fstr, " mbox=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tns:
		fmtprint(&fstr,  " ns=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tmg:
	case Tmr:
		fmtprint(&fstr, " mbox=%s", idnname(rp->mb, buf, sizeof(buf)));
		break;
	case Tminfo:
		fmtprint(&fstr, " mbox=%s mbox=%s",
			idnname(rp->mb, buf, sizeof(buf)),
			idnname(rp->rmb, buf, sizeof(buf)));
		break;
	case Tmx:
		fmtprint(&fstr, " pref=%lud mx=%s", rp->pref,
			idnname(rp->host, buf, sizeof(buf)));
		break;
	case Ta:
	case Taaaa:
		fmtprint(&fstr, " ip=%s", dnname(rp->ip));
		break;
	case Tptr:
		fmtprint(&fstr, " dom=%s", dnname(rp->ptr));
		break;
	case Tsoa:
		soa = rp->soa;
		fmtprint(&fstr,
" ns=%s mbox=%s serial=%lud refresh=%lud retry=%lud expire=%lud ttl=%lud",
			idnname(rp->host, buf, sizeof(buf)),
			idnname(rp->rmb, buf, sizeof(buf)),
			(soa? soa->serial: 0),
			(soa? soa->refresh: 0), (soa? soa->retry: 0),
			(soa? soa->expire: 0), (soa? soa->minttl: 0));
		for(s = soa->slaves; s != nil; s = s->next)
			fmtprint(&fstr, " dnsslave=%s", s->name);
		break;
	case Tsrv:
		srv = rp->srv;
		fmtprint(&fstr, " pri=%ud weight=%ud port=%ud target=%s",
			(srv? srv->pri: 0), (srv? srv->weight: 0),
			rp->port, idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tnull:
		if (rp->null == nil)
			fmtprint(&fstr, " null=<null>");
		else
			fmtprint(&fstr, " null=%.*H", rp->null->dlen,
				rp->null->data);
		break;
	case Ttxt:
		fmtprint(&fstr, " txt=");
		quote = 0;
		for(t = rp->txt; t != nil; t = t->next)
			if(strchr(t->p, ' '))
				quote = 1;
		if(quote)
			fmtprint(&fstr, "\"");
		for(t = rp->txt; t != nil; t = t->next)
			fmtprint(&fstr, "%s", t->p);
		if(quote)
			fmtprint(&fstr, "\"");
		break;
	case Trp:
		fmtprint(&fstr, " rp=%s txt=%s",
			idnname(rp->rmb, buf, sizeof(buf)),
			idnname(rp->rp, buf, sizeof(buf)));
		break;
	case Tkey:
		if (rp->key == nil)
			fmtprint(&fstr, " flags=<null> proto=<null> alg=<null>");
		else
			fmtprint(&fstr, " flags=%d proto=%d alg=%d",
				rp->key->flags, rp->key->proto, rp->key->alg);
		break;
	case Tsig:
		if (rp->sig == nil)
			fmtprint(&fstr,
" type=<null> alg=<null> labels=<null> ttl=<null> exp=<null> incep=<null> tag=<null> signer=<null>");
		else
			fmtprint(&fstr,
" type=%d alg=%d labels=%d ttl=%lud exp=%lud incep=%lud tag=%d signer=%s",
				rp->sig->type, rp->sig->alg, rp->sig->labels,
				rp->sig->ttl, rp->sig->exp, rp->sig->incep,
				rp->sig->tag, idnname(rp->sig->signer, buf, sizeof(buf)));
		break;
	case Tcert:
		if (rp->cert == nil)
			fmtprint(&fstr, " type=<null> tag=<null> alg=<null>");
		else
			fmtprint(&fstr, " type=%d tag=%d alg=%d",
				rp->cert->type, rp->cert->tag, rp->cert->alg);
		break;
	}
out:
	strp = fmtstrflush(&fstr);
	rv = fmtstrcpy(f, strp);
	free(strp);
	return rv;
}

void
warning(char *fmt, ...)
{
	char dnserr[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(dnserr, dnserr+sizeof(dnserr), fmt, arg);
	va_end(arg);
	syslog(1, logfile, dnserr);		/* on console too */
}

void
dnslog(char *fmt, ...)
{
	char dnserr[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(dnserr, dnserr+sizeof(dnserr), fmt, arg);
	va_end(arg);
	syslog(0, logfile, dnserr);
}

/*
 * based on libthread's threadsetname, but drags in less library code.
 * actually just sets the arguments displayed.
 */
void
procsetname(char *fmt, ...)
{
	int fd;
	char *cmdname;
	char buf[128];
	va_list arg;

	va_start(arg, fmt);
	cmdname = vsmprint(fmt, arg);
	va_end(arg);
	if (cmdname == nil)
		return;
	snprint(buf, sizeof buf, "#p/%d/args", getpid());
	if((fd = sys_open(buf, OWRITE)) >= 0){
		jehanne_write(fd, cmdname, strlen(cmdname)+1);
		sys_close(fd);
	}
	free(cmdname);
}

/*
 *  create a slave process to handle a request to avoid one request blocking
 *  another
 */
void
slave(Request *req)
{
	int ppid, procs;

	if(req->isslave)
		return;		/* we're already a slave process */

	/*
	 * These calls to putactivity cannot block.
	 * After getactivity(), the current process is counted
	 * twice in dnvars.active (one will pass to the child).
	 * If putactivity tries to wait for dnvars.active == 0,
	 * it will never happen.
	 */

	/* limit parallelism */
	procs = getactivity(req, 1);
	if(procs > stats.slavehiwat)
		stats.slavehiwat = procs;
	if(procs > Maxactive){
		if(traceactivity)
			dnslog("[%d] too much activity", getpid());
		putactivity(1);
		return;
	}

	/*
	 * parent returns to main loop, child does the work.
	 * don't change note group.
	 */
	ppid = getpid();
	switch(sys_rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		putactivity(1);
		break;
	case 0:
		procsetname("request slave of pid %d", ppid);
 		if(traceactivity)
			dnslog("[%d] take activity from %d", getpid(), ppid);
		req->isslave = 1;	/* why not `= getpid()'? */
		break;
	default:
		/*
		 * this relies on rfork producing separate, initially-identical
		 * stacks, thus giving us two copies of `req', one in each
		 * process.
		 */
		sys_alarm(0);
		longjmp(req->mret, 1);
	}
}

static int
rrequiv(RR *r1, RR *r2)
{
	return r1->owner == r2->owner
		&& r1->type == r2->type
		&& r1->arg0 == r2->arg0
		&& r1->arg1 == r2->arg1;
}

void
unique(RR *rp)
{
	RR **l, *nrp;

	for(; rp; rp = rp->next){
		l = &rp->next;
		for(nrp = *l; nrp; nrp = *l)
			if(rrequiv(rp, nrp)){
				*l = nrp->next;
				rrfree(nrp);
			} else
				l = &nrp->next;
	}
}

/*
 *  true if second domain is subsumed by the first
 */
int
subsume(char *higher, char *lower)
{
	int hn, ln;

	ln = strlen(lower);
	hn = strlen(higher);
	if (ln < hn || cistrcmp(lower + ln - hn, higher) != 0 ||
	    ln > hn && hn != 0 && lower[ln - hn - 1] != '.')
		return 0;
	return 1;
}

/*
 *  randomize the order we return items to provide some
 *  load balancing for servers.
 *
 *  only randomize the first class of entries
 */
RR*
randomize(RR *rp)
{
	RR *first, *last, *x, *base;
	uint32_t n;

	if(rp == nil || rp->next == nil)
		return rp;

	/* just randomize addresses, mx's and ns's */
	for(x = rp; x; x = x->next)
		if(x->type != Ta && x->type != Taaaa &&
		    x->type != Tmx && x->type != Tns)
			return rp;

	base = rp;

	n = rand();
	last = first = nil;
	while(rp != nil){
		/* stop randomizing if we've moved past our class */
		if(base->auth != rp->auth || base->db != rp->db){
			last->next = rp;
			break;
		}

		/* unchain */
		x = rp;
		rp = x->next;
		x->next = nil;

		if(n&1){
			/* add to tail */
			if(last == nil)
				first = x;
			else
				last->next = x;
			last = x;
		} else {
			/* add to head */
			if(last == nil)
				last = x;
			x->next = first;
			first = x;
		}

		/* reroll the dice */
		n >>= 1;
	}

	return first;
}

static int
sencodefmt(Fmt *f)
{
	int i, len, ilen, rv;
	char *out, *buf;
	uint8_t *b;
	char obuf[64];		/* rsc optimization */

	if(!(f->flags&FmtPrec) || f->prec < 1)
		goto error;

	b = va_arg(f->args, uint8_t*);
	if(b == nil)
		goto error;

	/* if it's a printable, go for it */
	len = f->prec;
	for(i = 0; i < len; i++)
		if(!isprint(b[i]))
			break;
	if(i == len){
		if(len >= sizeof obuf)
			len = sizeof(obuf)-1;
		memmove(obuf, b, len);
		obuf[len] = 0;
		fmtstrcpy(f, obuf);
		return 0;
	}

	ilen = f->prec;
	f->prec = 0;
	f->flags &= ~FmtPrec;
	len = 2*ilen + 1;
	if(len > sizeof(obuf)){
		buf = malloc(len);
		if(buf == nil)
			goto error;
	} else
		buf = obuf;

	/* convert */
	out = buf;
	rv = enc16(out, len, b, ilen);
	if(rv < 0)
		goto error;

	fmtstrcpy(f, buf);
	if(buf != obuf)
		free(buf);
	return 0;

error:
	return fmtstrcpy(f, "<encodefmt>");
}

void*
emalloc(int size)
{
	char *x;

	x = mallocz(size, 1);
	if(x == nil)
		abort();
	setmalloctag(x, getcallerpc());
	return x;
}

char*
estrdup(char *s)
{
	int size;
	char *p;

	size = strlen(s);
	p = mallocz(size+1, 0);
	if(p == nil)
		abort();
	memmove(p, s, size);
	p[size] = 0;
	setmalloctag(p, getcallerpc());
	return p;
}

/*
 *  create a pointer record
 */
static RR*
mkptr(DN *dp, char *ptr, uint32_t ttl)
{
	DN *ipdp;
	RR *rp;

	ipdp = dnlookup(ptr, Cin, 1);

	rp = rralloc(Tptr);
	rp->ptr = dp;
	rp->owner = ipdp;
	rp->db = 1;
	if(ttl)
		rp->ttl = ttl;
	return rp;
}

void	bytes2nibbles(uint8_t *nibbles, uint8_t *bytes, int nbytes);

/*
 *  look for all ip addresses in this network and make
 *  pointer records for them.
 */
void
dnptr(uint8_t *net, uint8_t *mask, char *dom, int forwtype, int subdoms, int ttl)
{
	int i, j, len;
	char *p, *e;
	char ptr[Domlen];
	uint8_t *ipp;
	uint8_t ip[IPaddrlen], nnet[IPaddrlen];
	uint8_t nibip[IPaddrlen*2];
	DN *dp;
	RR *rp, *nrp, *first, **l;

	l = &first;
	first = nil;
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next)
			for(rp = dp->rr; rp; rp = rp->next){
				if(rp->type != forwtype || rp->negative)
					continue;
				parseip(ip, rp->ip->name);
				maskip(ip, mask, nnet);
				if(ipcmp(net, nnet) != 0)
					continue;

				ipp = ip;
				len = IPaddrlen;
				if (forwtype == Taaaa) {
					bytes2nibbles(nibip, ip, IPaddrlen);
					ipp = nibip;
					len = 2*IPaddrlen;
				}

				p = ptr;
				e = ptr+sizeof(ptr);
				for(j = len - 1; j >= len - subdoms; j--)
					p = seprint(p, e, (forwtype == Ta?
						"%d.": "%x."), ipp[j]);
				seprint(p, e, "%s", dom);

				nrp = mkptr(dp, ptr, ttl);
				*l = nrp;
				l = &nrp->next;
			}

	for(rp = first; rp != nil; rp = nrp){
		nrp = rp->next;
		rp->next = nil;
		dp = rp->owner;
		rrattach(rp, Authoritative);
		dnagenever(dp);
	}
}

void
addserver(Server **l, char *name)
{
	Server *s;
	int n;

	while(*l)
		l = &(*l)->next;
	n = strlen(name);
	s = malloc(sizeof(Server)+n+1);
	if(s == nil)
		return;
	s->name = (char*)(s+1);
	memmove(s->name, name, n);
	s->name[n] = 0;
	s->next = nil;
	*l = s;
}

Server*
copyserverlist(Server *s)
{
	Server *ns;

	for(ns = nil; s != nil; s = s->next)
		addserver(&ns, s->name);
	return ns;
}


/* from here down is copied to ip/snoopy/dns.c periodically to update it */

/*
 *  convert an integer RR type to it's ascii name
 */
char*
rrname(int type, char *buf, int len)
{
	char *t;

	t = nil;
	if(type >= 0 && type <= Tall)
		t = rrtname[type];
	if(t==nil){
		snprint(buf, len, "%d", type);
		t = buf;
	}
	return t;
}

/*
 *  free a list of resource records and any related structs
 */
void
rrfreelist(RR *rp)
{
	RR *next;

	for(; rp; rp = next){
		next = rp->next;
		rrfree(rp);
	}
}

void
freeserverlist(Server *s)
{
	Server *next;

	for(; s != nil; s = next){
		next = s->next;
		memset(s, 0, sizeof *s);	/* cause trouble */
		free(s);
	}
}

/*
 *  allocate a resource record of a given type
 */
RR*
rralloc(int type)
{
	RR *rp;

	rp = emalloc(sizeof(*rp));
	rp->magic = RRmagic;
	rp->pc = getcallerpc();
	rp->type = type;
	if (rp->type != type)
		dnslog("rralloc: bogus type %d", type);
	setmalloctag(rp, rp->pc);
	switch(type){
	case Tsoa:
		rp->soa = emalloc(sizeof(*rp->soa));
		rp->soa->slaves = nil;
		setmalloctag(rp->soa, rp->pc);
		break;
	case Tsrv:
		rp->srv = emalloc(sizeof(*rp->srv));
		setmalloctag(rp->srv, rp->pc);
		break;
	case Tkey:
		rp->key = emalloc(sizeof(*rp->key));
		setmalloctag(rp->key, rp->pc);
		break;
	case Tcert:
		rp->cert = emalloc(sizeof(*rp->cert));
		setmalloctag(rp->cert, rp->pc);
		break;
	case Tsig:
		rp->sig = emalloc(sizeof(*rp->sig));
		setmalloctag(rp->sig, rp->pc);
		break;
	case Tnull:
		rp->null = emalloc(sizeof(*rp->null));
		setmalloctag(rp->null, rp->pc);
		break;
	}
	rp->ttl = 0;
	rp->expire = 0;
	rp->next = 0;
	return rp;
}

/*
 *  free a resource record and any related structs
 */
void
rrfree(RR *rp)
{
	Txt *t;

	assert(rp->magic == RRmagic && !rp->cached);

	switch(rp->type){
	case Tsoa:
		freeserverlist(rp->soa->slaves);
		memset(rp->soa, 0, sizeof *rp->soa);	/* cause trouble */
		free(rp->soa);
		break;
	case Tsrv:
		memset(rp->srv, 0, sizeof *rp->srv);	/* cause trouble */
		free(rp->srv);
		break;
	case Tkey:
		free(rp->key->data);
		memset(rp->key, 0, sizeof *rp->key);	/* cause trouble */
		free(rp->key);
		break;
	case Tcert:
		free(rp->cert->data);
		memset(rp->cert, 0, sizeof *rp->cert);	/* cause trouble */
		free(rp->cert);
		break;
	case Tsig:
		free(rp->sig->data);
		memset(rp->sig, 0, sizeof *rp->sig);	/* cause trouble */
		free(rp->sig);
		break;
	case Tnull:
		free(rp->null->data);
		memset(rp->null, 0, sizeof *rp->null);	/* cause trouble */
		free(rp->null);
		break;
	case Ttxt:
		while(t = rp->txt){
			rp->txt = t->next;
			free(t->p);
			memset(t, 0, sizeof *t);	/* cause trouble */
			free(t);
		}
		break;
	}

	memset(rp, 0, sizeof *rp);		/* cause trouble */
	rp->magic = ~RRmagic;
	free(rp);
}
