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

enum
{
	Maxenvsize = 16300,
};

static Egrp	*envgrp(Chan *c);
static int	envwriteable(Chan *c);

static Egrp	confegrp;	/* global environment group containing the kernel configuration */

static Evalue*
envlookup(Egrp *eg, char *name, uint32_t qidpath)
{
	Evalue *e;
	int i;

	for(i=0; i<eg->nent; i++){
		e = eg->ent[i];
		if(e->qid.path == qidpath || (name && e->name[0]==name[0] && jehanne_strcmp(e->name, name) == 0))
			return e;
	}
	return nil;
}

static int
envgen(Chan *c, char *name, Dirtab* _1, int _2, int s, Dir *dp)
{
	Egrp *eg;
	Evalue *e = nil;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, "#e", 0, eve, DMDIR|0775, dp);
		return 1;
	}

	eg = envgrp(c);
	rlock(&eg->rwl);
	if(name != nil)
		e = envlookup(eg, name, -1);
	else if(s < eg->nent)
		e = eg->ent[s];

	if(e == nil || name != nil && (jehanne_strlen(e->name) >= sizeof(up->genbuf))) {
		runlock(&eg->rwl);
		return -1;
	}

	/* make sure name string continues to exist after we release lock */
	kstrcpy(up->genbuf, e->name, sizeof up->genbuf);
	devdir(c, e->qid, up->genbuf, e->len, eve, 0666, dp);
	runlock(&eg->rwl);
	return 1;
}

static Chan*
envattach(Chan *c, Chan *ac, char *spec, int flags)
{
	Egrp *egrp = nil;

	if(spec && *spec) {
		if(jehanne_strcmp(spec, "c") != 0)
			error(Ebadarg);
		egrp = &confegrp;
	}

	c = devattach('e', spec);
	c->aux = egrp;
	return c;
}

static Walkqid*
envwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, envgen);
}

static long
envstat(Chan *c, uint8_t *db, long n)
{
	if(c->qid.type & QTDIR)
		c->qid.vers = envgrp(c)->vers;
	return devstat(c, db, n, 0, 0, envgen);
}

static Chan*
envopen(Chan *c, unsigned long omode)
{
	Egrp *eg;
	Evalue *e;
	int trunc;

	eg = envgrp(c);
	if(c->qid.type & QTDIR) {
		if(omode != OREAD)
			error(Eperm);
	}
	else {
		trunc = omode & OTRUNC;
		if(omode != OREAD && !envwriteable(c))
			error(Eperm);
		if(trunc)
			wlock(&eg->rwl);
		else
			rlock(&eg->rwl);
		e = envlookup(eg, nil, c->qid.path);
		if(e == nil) {
			if(trunc)
				wunlock(&eg->rwl);
			else
				runlock(&eg->rwl);
			error(Enonexist);
		}
		if(trunc && e->value) {
			e->qid.vers++;
			jehanne_free(e->value);
			e->value = nil;
			e->len = 0;
		}
		if(trunc)
			wunlock(&eg->rwl);
		else
			runlock(&eg->rwl);
	}
	c->mode = openmode(omode);
	incref(&eg->r);
	c->aux = eg;
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static Chan*
envcreate(Chan *c, char *name, unsigned long omode, unsigned long _1)
{
	Egrp *eg;
	Evalue *e;
	Evalue **ent;

	if(c->qid.type != QTDIR || !envwriteable(c))
		error(Eperm);

	omode = openmode(omode);
	eg = envgrp(c);

	wlock(&eg->rwl);
	if(waserror()) {
		wunlock(&eg->rwl);
		nexterror();
	}

	if(envlookup(eg, name, -1))
		error(Eexist);

	e = smalloc(sizeof(Evalue));
	e->name = smalloc(jehanne_strlen(name)+1);
	jehanne_strcpy(e->name, name);

	if(eg->nent == eg->ment){
		eg->ment += 32;
		ent = smalloc(sizeof(eg->ent[0])*eg->ment);
		if(eg->nent)
			jehanne_memmove(ent, eg->ent, sizeof(eg->ent[0])*eg->nent);
		jehanne_free(eg->ent);
		eg->ent = ent;
	}
	e->qid.path = ++eg->path;
	e->qid.vers = 0;
	eg->vers++;
	eg->ent[eg->nent++] = e;
	c->qid = e->qid;

	wunlock(&eg->rwl);
	poperror();
	incref(&eg->r);
	c->aux = eg;
	c->offset = 0;
	c->mode = omode;
	c->flag |= COPEN;

	return c;
}

static void
envremove(Chan *c)
{
	int i;
	Egrp *eg;
	Evalue *e;

	if(c->qid.type & QTDIR || !envwriteable(c))
		error(Eperm);

	eg = envgrp(c);
	if(eg == nil){
		panic("envremove: no Egrp for %s", c->path->s);
	}
	wlock(&eg->rwl);
	e = nil;
	for(i=0; i<eg->nent; i++){
		if(eg->ent[i]->qid.path == c->qid.path){
			e = eg->ent[i];
			eg->nent--;
			eg->ent[i] = eg->ent[eg->nent];
			eg->vers++;
			break;
		}
	}
	wunlock(&eg->rwl);
	if(e == nil)
		error(Enonexist);
	jehanne_free(e->name);
	jehanne_free(e->value);
	jehanne_free(e);
}

static void
envclose(Chan *c)
{
	if(c->flag & COPEN){
		/*
		 * cclose can't fail, so errors from remove will be ignored.
		 * since permissions aren't checked,
		 * envremove can't not remove it if its there.
		 */
		if(c->flag & CRCLOSE && !waserror()){
			envremove(c);
			poperror();
		}
		closeegrp((Egrp*)c->aux);
		c->aux = nil;
	}
}

static long
envread(Chan *c, void *a, long n, int64_t off)
{
	Egrp *eg;
	Evalue *e;
	long offset;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, 0, 0, envgen);

	eg = envgrp(c);
	rlock(&eg->rwl);
	e = envlookup(eg, nil, c->qid.path);
	if(e == 0) {
		runlock(&eg->rwl);
		error(Enonexist);
	}

	offset = off;
	if(offset > e->len)	/* protects against overflow converting int64_t to long */
		n = 0;
	else if(offset + n > e->len)
		n = e->len - offset;
	if(n <= 0)
		n = 0;
	else
		jehanne_memmove(a, e->value+offset, n);
	runlock(&eg->rwl);
	return n;
}

static long
envwrite(Chan *c, void *a, long n, int64_t off)
{
	char *s;
	Egrp *eg;
	Evalue *e;
	long len, offset;

	if(n <= 0)
		return 0;
	offset = off;
	if(offset > Maxenvsize || n > (Maxenvsize - offset))
		error(Etoobig);

	eg = envgrp(c);
	wlock(&eg->rwl);
	e = envlookup(eg, nil, c->qid.path);
	if(e == 0) {
		wunlock(&eg->rwl);
		error(Enonexist);
	}

	len = offset+n;
	if(len > e->len) {
		s = smalloc(len);
		if(e->value){
			jehanne_memmove(s, e->value, e->len);
			jehanne_free(e->value);
		}
		e->value = s;
		e->len = len;
	}
	jehanne_memmove(e->value+offset, a, n);
	e->qid.vers++;
	eg->vers++;
	wunlock(&eg->rwl);
	return n;
}

Dev envdevtab = {
	'e',
	"env",

	devreset,
	devinit,
	devshutdown,
	envattach,
	envwalk,
	envstat,
	envopen,
	envcreate,
	envclose,
	envread,
	devbread,
	envwrite,
	devbwrite,
	envremove,
	devwstat,
};

void
envcpy(Egrp *to, Egrp *from)
{
	int i;
	Evalue *ne, *e;

	rlock(&from->rwl);
	to->ment = (from->nent+31)&~31;
	to->ent = smalloc(to->ment*sizeof(to->ent[0]));
	for(i=0; i<from->nent; i++){
		e = from->ent[i];
		ne = smalloc(sizeof(Evalue));
		ne->name = smalloc(jehanne_strlen(e->name)+1);
		jehanne_strcpy(ne->name, e->name);
		if(e->value){
			ne->value = smalloc(e->len);
			jehanne_memmove(ne->value, e->value, e->len);
			ne->len = e->len;
		}
		ne->qid.path = ++to->path;
		to->ent[i] = ne;
	}
	to->nent = from->nent;
	runlock(&from->rwl);
}

void
closeegrp(Egrp *eg)
{
	int i;
	Evalue *e;

	if(decref(&eg->r) == 0 && eg != &confegrp){
		for(i=0; i<eg->nent; i++){
			e = eg->ent[i];
			jehanne_free(e->name);
			jehanne_free(e->value);
			jehanne_free(e);
		}
		jehanne_free(eg->ent);
		jehanne_free(eg);
	}
}

static Egrp*
envgrp(Chan *c)
{
	if(c->aux == nil)
		return up->egrp;
	return c->aux;
}

static int
envwriteable(Chan *c)
{
	return isevegroup() || c->aux == nil;
}

/*
 *  to let the kernel set environment variables
 */
void
ksetenv(char *ename, char *eval, int conf)
{
	Chan *c;
	char buf[2*KNAMELEN];

	jehanne_snprint(buf, sizeof(buf), "#e%s/%s", conf?"c":"", ename);
	c = namec(buf, Acreate, OWRITE, 0600);
	c->dev->write(c, eval, jehanne_strlen(eval), 0);
	cclose(c);
}

/*
 * Return a copy of configuration environment as a sequence of strings.
 * The strings alternate between name and value.  A zero length name string
 * indicates the end of the list
 */
char *
getconfenv(void)
{
	Egrp *eg = &confegrp;
	Evalue *e;
	char *p, *q;
	int i, n;

	rlock(&eg->rwl);
	if(waserror()) {
		runlock(&eg->rwl);
		nexterror();
	}

	/* determine size */
	n = 0;
	for(i=0; i<eg->nent; i++){
		e = eg->ent[i];
		n += jehanne_strlen(e->name) + e->len + 2;
	}
	p = jehanne_malloc(n + 1);
	if(p == nil)
		error(Enomem);
	q = p;
	for(i=0; i<eg->nent; i++){
		e = eg->ent[i];
		jehanne_strcpy(q, e->name);
		q += jehanne_strlen(q) + 1;
		jehanne_memmove(q, e->value, e->len);
		q[e->len] = 0;
		/* move up to the first null */
		q += jehanne_strlen(q) + 1;
	}
	*q = 0;

	poperror();
	runlock(&eg->rwl);
	return p;
}
