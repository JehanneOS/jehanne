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

#define INT_MAX ((1<<31)-1)
extern int sysdup(int ofd, int nfd);

/* Qid is (2*fd + (file is ctl))+1 */

static int
dupgen(Chan *c, char * _1, Dirtab* _2, int _3, int s, Dir *dp)
{
	Fgrp *fgrp = up->fgrp;
	Chan *f;
	static int perm[] = { 0, 0400, 0200, 0600, 0 };
	int p;
	Qid q;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, ".", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	if(s == 0)
		return 0;
	s--;
	if(s/2 > fgrp->maxfd)
		return -1;
	if((f=fgrp->fd[s/2]) == nil)
		return 0;
	if(s & 1){
		p = 0400;
		jehanne_sprint(up->genbuf, "%dctl", s/2);
	}else{
		p = perm[f->mode&7];
		jehanne_sprint(up->genbuf, "%d", s/2);
	}
	mkqid(&q, s+1, 0, QTFILE);
	devdir(c, q, up->genbuf, 0, eve, p, dp);
	return 1;
}

static Chan*
dupattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('d', spec);
}

static Walkqid*
dupwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, (Dirtab *)0, 0, dupgen);
}

static long
dupstat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, (Dirtab *)0, 0L, dupgen);
}

static Chan*
dupcreate(Chan* c, char* name, unsigned long omode, unsigned long perm)
{
	FdPair in, out;

	if((omode|OCEXEC) == ~0 && c->qid.path == 0){
		in.aslong = perm;
		/* ensure that fd.aslong never get ~0 value */
		if(in.fd[1] == 0){
			/* the requested fd is zero: out.fd[0] will
			 * prevent the whole fd.aslong to become -1
			 * if the operation succeed
			 */
			out.fd[0] = -1;
		} else {
			out.fd[0] = 0;
		}
		out.fd[1] = sysdup(in.fd[0], in.fd[1]);
		errorl(nil, ~out.aslong);
	}
	error(Eperm);
}

static Chan*
dupopen(Chan *c, unsigned long omode)
{
	Chan *f;
	int fd, twicefd;

	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Eisdir);
		c->mode = OREAD;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	if(c->qid.type & QTAUTH)
		error(Eperm);
	twicefd = c->qid.path - 1;
	fd = twicefd/2;
	if((twicefd & 1)){
		/* ctl file */
		f = c;
		f->mode = openmode(omode);
		f->flag |= COPEN;
		f->offset = 0;
	}else{
		/* fd file */
		f = fdtochan(fd, openmode(omode), 0, 1);
		cclose(c);
	}
	if(omode & OCEXEC)
		f->flag |= CCEXEC;
	return f;
}

static void
dupclose(Chan* _1)
{
}

static long
dupread(Chan *c, void *va, long n, int64_t off)
{
	char buf[256];
	int fd, twicefd;

	if(c->qid.type & QTDIR)
		return devdirread(c, va, n, (Dirtab *)0, 0L, dupgen);
	twicefd = c->qid.path - 1;
	fd = twicefd/2;
	if(twicefd & 1){
		c = fdtochan(fd, -1, 0, 1);
		procfdprint(c, fd, 0, buf, sizeof buf);
		cclose(c);
		return readstr(off, va, n, buf);
	}
	panic("dupread");
	return 0;
}

static long
dupwrite(Chan* _1, void* _2, long _3, int64_t _4)
{
	error(Eperm);
	return 0;		/* not reached */
}

Dev dupdevtab = {
	'd',
	"dup",

	devreset,
	devinit,
	devshutdown,
	dupattach,
	dupwalk,
	dupstat,
	dupopen,
	dupcreate,
	dupclose,
	dupread,
	devbread,
	dupwrite,
	devbwrite,
	devremove,
	devwstat,
};
