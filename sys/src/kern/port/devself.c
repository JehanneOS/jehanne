/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define QID(q)		((((uint32_t)(q).path)&0x0000001F)>>0)
#define STATSIZE	(2*KNAMELEN+NUMSIZE+9*NUMSIZE + 1)

/* devself provides to each process access to its own structures
 * (pid, ppid, segments...).
 */
typedef enum SelfNodes
{
	Qdir,

	/* Process control */
	Qbrk,
	Qpid,
	Qppid,
	Qpgrpid,
	Qsegments,
	Qpipes,
	Qwdir,

	/* safe resources */
	Qnull,
	Qzero,

} SelfNodes;

typedef enum SegmentsCmd
{
	CMsegattach,
	CMsegdetach,
	CMsegfree,
} SegmentsCmd;

static
Cmdtab proccmd[] = {
	CMsegattach,		"attach",		5,
	CMsegdetach,		"detach",		2,
	CMsegfree,		"free",			3,
};

static Dirtab selfdir[]={
	".",		{Qdir, 0, QTDIR},	0,		DMDIR|0777,
	"brk",		{Qbrk, 0, QTDIR},	0,		DMDIR|0300,
	"pid",		{Qpid},			0,		0400,
	"ppid",		{Qppid},		0,		0400,
	"pgrpid",	{Qpgrpid},		0,		0400,
	"segments",	{Qsegments},		0,		0644,
	"pipes",	{Qpipes},		0,		0,
	"wdir",		{Qwdir},		0,		0644,

	"null",		{Qnull},		0,		0666,
	"zero",		{Qzero},		0,		0444,
};


static int
selfgen(Chan *c, char *name, Dirtab *tab, int ntab, int i, Dir *dp)
{
	long length;
	if(tab == 0)
		return -1;
	if(i == DEVDOTDOT){
		if(QID(c->qid) != Qdir)
			panic("selfwalk %llux", c->qid.path);
		devdir(c, selfdir[0].qid, "#0", 0, up->user, selfdir[0].perm, dp);
		return 1;
	}

	if(name){
		for(i=1; i<ntab; i++)
			if(jehanne_strcmp(tab[i].name, name) == 0)
				break;
		if(i==ntab)
			return -1;
		tab += i;
	}else{
		/* skip over the first element, that for . itself */
		i++;
		if(i >= ntab)
			return -1;
		tab += i;
	}
	if(tab->qid.path == Qwdir) {
		/* file length might be relevant to the caller to
		 * malloc enough space in the buffer
		 */
		length = 1 + jehanne_strlen(up->dot->path->s);
	} else {
		length = tab->length;
	}
	devdir(c, tab->qid, tab->name, length, up->user, tab->perm, dp);
	return 1;
}

static Chan*
selfattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('0', spec);
}

static uintptr_t
grow_bss(uintptr_t addr)
{
	ProcSegment *s, *ns;
	uintptr_t newtop;
	long newsize;
	int i;

	s = up->seg[BSEG];
	if(s == nil)
		panic("grow_bss: no bss segment");

	if(addr == 0)
		return s->top;

	qlock(&s->ql);
	if(waserror()){
		qunlock(&s->ql);
		nexterror();
	}

	DBG("grow_bss addr %#p base %#p top %#p\n",
		addr, s->base, s->top);
	/* We may start with the bss overlapping the data */
	if(addr < s->base) {
		if(up->seg[DSEG] == 0 || addr < up->seg[DSEG]->base)
			error(Enovmem);
		addr = s->base;
	}

	newtop = ROUNDUP(addr, PGSZ);
	newsize = (newtop - s->table->base)/PGSZ;


	DBG("grow_bss addr %#p newtop %#p newsize %ld\n", addr, newtop, newsize);

	if(newtop < s->top) {
		/* for simplicity we only allow the bss to grow,
		 * memory will be freed on process exit
		 */
		panic("grow_bss: shrinking bss");
	}

	rlock(&up->seglock);
	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == 0 || ns == s)
			continue;
		if(newtop >= ns->base && newtop < ns->top){
			runlock(&up->seglock);
			error(Esoverlap);
		}
	}
	runlock(&up->seglock);

	if(!umem_available(newtop - s->top))
		error(Enovmem);

	if(!segment_grow(s, newtop))
		error(Enovmem);

	poperror();
	qunlock(&s->ql);

	return s->top;
}

static Chan*
selfcreate(Chan* c, char* name, unsigned long omode, unsigned long perm)
{
	long e;

	switch(QID(c->qid)){
	default:
		error(Eperm);
	case Qbrk:
		if(jehanne_strcmp(name, "set") != 0)
			error(Eperm);
		e = (long)grow_bss(perm);
		errorl(nil, ~e);
	}
}

static Walkqid*
selfwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, selfdir, nelem(selfdir), selfgen);
}

static long
selfstat(Chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, selfdir, nelem(selfdir), selfgen);
}

static Chan*
selfopen(Chan *c, unsigned long omode)
{
	c->aux = nil;
	c = devopen(c, omode, selfdir, nelem(selfdir), selfgen);
	return c;
}

long
read_working_dir(Proc* p, void *va, long n, int64_t off)
{
	int i, j;
	char *path;
	Chan *dot;

	dot = up->dot;
	path = dot->path->s;
	i = 1 + jehanne_strlen(path);
	if(va == nil){
		/* the user is actually asking for the space */
		if(off != 0 && off != ~0) {
			/* #0/wdir does not allow offset in read */
			error("offset reading wdir size");
		}
		errorl("not enough space in buffer", ~i);
	}
	if(off > i)
		return 0;
	j = i - off;
	if(n < j)
		j = n;
	jehanne_memmove(va, path, j);

	return j;
}


long
write_working_dir(Proc* p, void *va, long n, int64_t off)
{
	Chan *c, *dot;
	char *path, *epath;

	dot = p->dot;
	path = va;
	epath = vmemchr(path, 0, n);

	if(n <= 0)
		error(Ebadarg);
	if(off != 0 && off != ~0)
		error("offset writing wdir");
	if(epath-path>=n)
		error("no terminal zero writing wdir");

	c = namec(path, Atodir, 0, 0);
	if(CASV(&p->dot, dot, c)){
		cclose(dot);
	} else {
		cclose(c);
		error("race writing wdir");
	}

	return 0;
}

static long
selfread(Chan *c, void *va, long n, int64_t offset)
{
	ProcSegment *sg;
	int i, j;
	char statbuf[NSEG*STATSIZE];
	
	if(offset < 0)
		error("invalid offset");

	switch(QID(c->qid)){
	case Qdir:
		return devdirread(c, va, n, selfdir, nelem(selfdir), selfgen);
	case Qsegments:
		j = 0;
		rlock(&up->seglock);
		for(i = 0; i < NSEG; i++) {
			sg = up->seg[i];
			if(sg == 0)
				continue;
			j += jehanne_sprint(statbuf+j, "%-6s %c%c %p %p %4d\n",
				segment_name(sg),
				!(sg->type&SgWrite) ? 'R' : ' ',
				(sg->type&SgExecute) ? 'x' : ' ',
				' ', // sg->profile ? 'P' : ' ', // here was profiling
				sg->base, sg->top, sg->r.ref);
		}
		runlock(&up->seglock);
		if(offset >= j)
			return 0;
		if(offset+n > j)
			n = j-offset;
		if(n == 0 && offset == 0)
			exhausted("segments");
		jehanne_memmove(va, &statbuf[offset], n);
		return n;
	case Qwdir:
		return read_working_dir(up, va, n, offset);

	case Qpid:
		return readnum(offset, va, n, up->pid, NUMSIZE);
	case Qppid:
		return readnum(offset, va, n, up->parentpid, NUMSIZE);
	case Qpgrpid:
		return readnum(offset, va, n, up->pgrp->pgrpid, NUMSIZE);

	case Qnull:
		return 0;
	case Qzero:
		jehanne_memset(va, 0, n);
		return n;

	default:
		error(Egreg);
	}
}

static uintptr_t
segattach(Proc* p, int attr, const char* name, uintptr_t va, usize len)
{
	unsigned int sno, tmp;
	ProcSegment *s;
	uintptr_t pa, size;


	if((va != 0 && va < UTZERO) || iskaddr(va))
		error("virtual address below text or in kernel");

	vmemchr((void *)name, 0, ~0);

	for(sno = 0; sno < NSEG; sno++)
		if(p->seg[sno] == nil && sno != ESEG)
			break;

	if(sno == NSEG)
		error("too many segments in process");

	len = ROUNDUP(va+len, PGSZ) - ROUNDDN(va, PGSZ);
	va = ROUNDDN(va, PGSZ);

	if(rawmem_find((char**)&name, &pa, &tmp, &size)){
		s = 0;
		wlock(&p->seglock);
		if(pa){
			if(!segment_physical(&s, attr&SegPermissionMask, attr&SegFlagMask, va, pa))
				goto SegAttachEbadarg;
		} else if(size != -1){
			if(!segment_global(&s, attr&SegPermissionMask, va, (char*)name))
				goto SegAttachEbadarg;
		} else {
			if(!segment_virtual(&s, tmp&SegmentTypesMask, attr&SegPermissionMask&~SgExecute, attr&SegFlagMask, va, va+len))
				goto SegAttachEbadarg;
		}
		for(tmp = 0; tmp < NSEG; tmp++){
			if(p->seg[tmp])
			if((p->seg[tmp]->base > s->base && p->seg[tmp]->base < s->top)
			|| (p->seg[tmp]->top > s->base && p->seg[tmp]->top < s->top)){
				goto SegAttachEsoverlap;
			}
		}
		up->seg[sno] = s;
		wunlock(&p->seglock);
		return s->base;
	}
	error("segment not found");
SegAttachEbadarg:
	wunlock(&p->seglock);
	error(Ebadarg);
SegAttachEsoverlap:
	wunlock(&p->seglock);
	segment_release(&s);
	error(Esoverlap);
}

static uintptr_t
segdetach(Proc* p, uintptr_t va)
{
	if(!proc_segment_detach(p, va))
		error(Ebadarg);

	mmuflush();
	return 0;
}

static uintptr_t
segfree(Proc* p, uintptr_t va, unsigned long len)
{
	ProcSegment *s;
	uintptr_t from, to;

	from = PTR2UINT(va);
	to = ROUNDDN(from + len, PGSZ);

	s = proc_segment(p, from);
	if(s == nil || to < from || to > s->top)
		error(Ebadarg);

	from = ROUNDUP(from, PGSZ);
	if(from == to)
		return 0;

	segment_free_pages(s, from, to);
	mmuflush();
	return 0;
}

static long
procsegctl(Proc *p, char *va, int n)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	int attr;
	const char *class;
	uintptr_t vareq;
	unsigned long len;
	long result;

	cb = parsecmd(va, n);
	if(waserror()){
		jehanne_free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, proccmd, nelem(proccmd));
	switch(ct->index){
	default:
		error(Ebadctl);
		return -1;
	case CMsegattach:
		attr = jehanne_strtoul(cb->f[1], 0, 0);
		vareq = jehanne_strtoull(cb->f[2], 0, 0);
		len = jehanne_strtoull(cb->f[3], 0, 0);
		class = cb->f[4];
		result = segattach(p, attr, class, vareq, len);
		break;
	case CMsegdetach:
		vareq = jehanne_strtoull(cb->f[1], 0, 0);
		result = segdetach(p, vareq);
		break;
	case CMsegfree:
		vareq = jehanne_strtoull(cb->f[1], 0, 0);
		len = jehanne_strtoull(cb->f[2], 0, 0);
		result = segfree(p, vareq, len);
		break;
	}
	poperror();
	return result;
}

static long
selfwrite(Chan *c, void *va, long n, int64_t off)
{
	switch(QID(c->qid)){
	default:
		error(Egreg);
	case Qsegments:
		return procsegctl(up, va, n);
	case Qwdir:
		return write_working_dir(up, va, n, off);

	case Qnull:
		return n;
	}
}

static int
newfd2(int fd[2], Chan *c[2])
{
	extern int findfreefd(Fgrp *f, int start);
	extern void unlockfgrp(Fgrp *f);
	Fgrp *f;

	f = up->fgrp;
	lock(&f->l);
	fd[0] = findfreefd(f, 0);
	if(fd[0] < 0){
		unlockfgrp(f);
		return -1;
	}
	fd[1] = findfreefd(f, fd[0]+1);
	if(fd[1] < 0){
		unlockfgrp(f);
		return -1;
	}
	if(fd[1] > f->maxfd)
		f->maxfd = fd[1];
	f->fd[fd[0]] = c[0];
	f->fd[fd[1]] = c[1];
	unlockfgrp(f);

	return 0;
}

static long
newpipe(void)
{
	FdPair pipe;
	Chan *c[2];
	static char *datastr[] = {"data", "data1"};

	c[0] = namec("#|", Atodir, 0, 0);
	c[1] = nil;
	pipe.fd[0] = -1;
	pipe.fd[1] = -1;

	if(waserror()){
		cclose(c[0]);
		if(c[1])
			cclose(c[1]);
		nexterror();
	}
	c[1] = cclone(c[0]);
	if(walk(&c[0], datastr+0, 1, 1, nil) < 0)
		error(Egreg);
	if(walk(&c[1], datastr+1, 1, 1, nil) < 0)
		error(Egreg);
	c[0] = c[0]->dev->open(c[0], ORDWR);
	c[1] = c[1]->dev->open(c[1], ORDWR);
	if(newfd2(pipe.fd, c) < 0)
		error(Enofd);
	poperror();

	return pipe.aslong;
}

static void
selfremove(Chan* c)
{
	long pipeset;
	switch((uint32_t)c->qid.path){
	case Qsegments:
		error(Eperm);
		break;
	case Qpid:
		errorl("got pid", up->pid);
		break;
	case Qppid:
		errorl("got parent pid", up->parentpid);
		break;
	case Qpgrpid:
		errorl("got process group number", up->pgrp->pgrpid);
		break;
	case Qpipes:
		pipeset = newpipe();
		errorl("got new pipes", pipeset);
		break;
	}
}


static void
selfclose(Chan *c)
{
}


Dev selfdevtab = {
	'0',
	"self",

	devreset,
	devinit,
	devshutdown,
	selfattach,
	selfwalk,
	selfstat,
	selfopen,
	selfcreate,
	selfclose,
	selfread,
	devbread,
	selfwrite,
	devbwrite,
	selfremove,
	devwstat,
};
