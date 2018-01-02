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

#include	<libsec.h>

enum
{
	Hashlen=	SHA1dlen,
	Maxhash=	256,
};

/*
 *  if a process knows cap->cap, it can change user
 *  to capabilty->user.
 */
typedef struct Caphash	Caphash;
struct Caphash
{
	Caphash	*next;
	char		hash[Hashlen];
	uint32_t		ticks;
};

struct
{
	QLock;
	Caphash	*first;
	int	nhash;
} capalloc;

enum
{
	Qdir,
	Qhash,
	Quse,
};

/* caphash must be last */
Dirtab capdir[] =
{
	".",		{Qdir,0,QTDIR},	0,		DMDIR|0500,
	"capuse",	{Quse},		0,		0222,
	"caphash",	{Qhash},	0,		0200,
};
int ncapdir = nelem(capdir);

static Chan*
capattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach(L'¤', spec);
}

static Walkqid*
capwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, capdir, ncapdir, devgen);
}

static void
capremove(Chan *c)
{
	if(iseve() && c->qid.path == Qhash)
		ncapdir = nelem(capdir)-1;
	else
		error(Eperm);
}


static long
capstat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, capdir, ncapdir, devgen);
}

/*
 *  if the stream doesn't exist, create it
 */
static Chan*
capopen(Chan *c, unsigned long omode)
{
	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Ebadarg);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	switch((uint32_t)c->qid.path){
	case Qhash:
		if(!iseve())
			error(Eperm);
		break;
	}

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

/*
static char*
hashstr(uint8_t *hash)
{
	static char buf[2*Hashlen+1];
	int i;

	for(i = 0; i < Hashlen; i++)
		jehanne_sprint(buf+2*i, "%2.2ux", hash[i]);
	buf[2*Hashlen] = 0;
	return buf;
}
 */

static Caphash*
remcap(uint8_t *hash)
{
	Caphash *t, **l;

	qlock(&capalloc);

	/* find the matching capability */
	for(l = &capalloc.first; *l != nil;){
		t = *l;
		if(jehanne_memcmp(hash, t->hash, Hashlen) == 0)
			break;
		l = &t->next;
	}
	t = *l;
	if(t != nil){
		capalloc.nhash--;
		*l = t->next;
	}
	qunlock(&capalloc);

	return t;
}

/* add a capability, throwing out any old ones */
static void
addcap(uint8_t *hash)
{
	Caphash *p, *t, **l;

	p = smalloc(sizeof *p);
	jehanne_memmove(p->hash, hash, Hashlen);
	p->next = nil;
	p->ticks = m->ticks;

	qlock(&capalloc);

	/* trim extras */
	while(capalloc.nhash >= Maxhash){
		t = capalloc.first;
		if(t == nil)
			panic("addcap");
		capalloc.first = t->next;
		jehanne_free(t);
		capalloc.nhash--;
	}

	/* add new one */
	for(l = &capalloc.first; *l != nil; l = &(*l)->next)
		;
	*l = p;
	capalloc.nhash++;

	qunlock(&capalloc);
}

static void
capclose(Chan* _1)
{
}

static long
capread(Chan *c, void *va, long n, int64_t _1)
{
	switch((uint32_t)c->qid.path){
	case Qdir:
		return devdirread(c, va, n, capdir, ncapdir, devgen);

	default:
		error(Eperm);
		break;
	}
	return n;
}

static long
capwrite(Chan *c, void *va, long n, int64_t _1)
{
	Caphash *p;
	char *cp;
	uint8_t hash[Hashlen];
	char *key, *from, *to;
	char err[256];

	switch((uint32_t)c->qid.path){
	case Qhash:
		if(!iseve())
			error(Eperm);
		if(n < Hashlen)
			error(Eshort);
		jehanne_memmove(hash, va, Hashlen);
		addcap(hash);
		break;

	case Quse:
		/* copy key to avoid a fault in hmac_xx */
		cp = nil;
		if(waserror()){
			jehanne_free(cp);
			nexterror();
		}
		cp = smalloc(n+1);
		jehanne_memmove(cp, va, n);
		cp[n] = 0;

		from = cp;
		key = jehanne_strrchr(cp, '@');
		if(key == nil)
			error(Eshort);
		*key++ = 0;

		hmac_sha1((uint8_t*)from, jehanne_strlen(from), (uint8_t*)key, jehanne_strlen(key), hash, nil);

		p = remcap(hash);
		if(p == nil){
			jehanne_snprint(err, sizeof err, "invalid capability %s@%s", from, key);
			error(err);
		}

		/* if a from user is supplied, make sure it matches */
		to = jehanne_strchr(from, '@');
		if(to == nil){
			to = from;
		} else {
			*to++ = 0;
			if(jehanne_strcmp(from, up->user) != 0)
				error("capability must match user");
		}

		/* set user id */
		kstrdup(&up->user, to);
		up->basepri = PriNormal;

		jehanne_free(p);
		jehanne_free(cp);
		poperror();
		break;

	default:
		error(Eperm);
		break;
	}

	return n;
}

Dev capdevtab = {
	L'¤',
	"cap",

	devreset,
	devinit,
	devshutdown,
	capattach,
	capwalk,
	capstat,
	capopen,
	devcreate,
	capclose,
	capread,
	devbread,
	capwrite,
	devbwrite,
	capremove,
	devwstat
};
