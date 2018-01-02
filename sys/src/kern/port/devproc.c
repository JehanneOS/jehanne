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

#include	<trace.h>
#include	"ureg.h"

extern long write_working_dir(Proc* p, void *va, long n, int64_t off);
extern long read_working_dir(Proc* p, void *va, long n, int64_t off);

/* We can have up to 32 files in proc/n sice we dedicate 5 bits in Qid
 * to it (see QSHIFT)
 */
enum
{
	Qdir,
	Qtrace,
	Qargs,
	Qctl,
	Qfd,
	Qfpregs,
	Qkregs,
	Qmem,
	Qnote,
	Qnoteid,
	Qnotepg,
	Qns,
	Qproc,
	Qppid,
	Qregs,
	Qsegment,
	Qstatus,
	Qtext,
	Qwait,
	Qprofile,
	Qsyscall,
	Qwdir,
};

enum
{
	CMclose,
	CMclosefiles,
	CMfixedpri,
	CMhang,
	CMkill,
	CMnohang,
	CMnoswap,
	CMpri,
	CMprivate,
	CMprofile,
	CMstart,
	CMstartstop,
	CMstartsyscall,
	CMstop,
	CMwaitstop,
	CMwired,
	CMtrace,
};

enum{
	Nevents = 0x4000,
	Emask = Nevents - 1,
};

#define STATSIZE	(2*KNAMELEN+NUMSIZE+9*NUMSIZE + 1)
/* In Plan 9 status, fd, and ns were left fully readable (0444)
 * because of their use in debugging, particularly on shared servers.
 *
 * In Jehanne the process owner and the host owner can read
 * status and fd, but not others (0440).
 * TODO: allow per process stats and permissions.
 */
Dirtab procdir[] =
{
	"args",		{Qargs},	0,			0660,
	"ctl",		{Qctl},		0,			0000,
	"fd",		{Qfd},		0,			0440,
	"fpregs",	{Qfpregs},	0,			0000,
	"kregs",	{Qkregs},	sizeof(Ureg),		0400,
	"mem",		{Qmem},		0,			0000,
	"note",		{Qnote},	0,			0000,
	"noteid",	{Qnoteid},	0,			0664,
	"notepg",	{Qnotepg},	0,			0000,
	"ns",		{Qns},		0,			0640,
	"proc",		{Qproc},	0,			0400,
	"ppid",		{Qppid},	0,			0400,
	"regs",		{Qregs},	sizeof(Ureg),		0000,
	"segment",	{Qsegment},	0,			0444,
	"status",	{Qstatus},	STATSIZE,		0444,
	"text",		{Qtext},	0,			0000,
	"wait",		{Qwait},	0,			0400,
	"syscall",	{Qsyscall},	0,			0400,
	"wdir",		{Qwdir},	0,			0640,
};

static
Cmdtab proccmd[] = {
	CMclose,		"close",		2,
	CMclosefiles,		"closefiles",		1,
	CMfixedpri,		"fixedpri",		2,
	CMhang,			"hang",			1,
	CMnohang,		"nohang",		1,
	CMnoswap,		"noswap",		1,
	CMkill,			"kill",			1,
	CMpri,			"pri",			2,
	CMprivate,		"private",		1,
	CMstart,		"start",		1,
	CMstartstop,		"startstop",		1,
	CMstartsyscall,		"startsyscall",		1,
	CMstop,			"stop",			1,
	CMwaitstop,		"waitstop",		1,
	CMwired,		"wired",		2,
	CMtrace,		"trace",		0,
};

/*
 * Qids are, in path:
 *	 5 bits of file type (qids above)
 *	23 bits of process slot number + 1
 *	     in vers,
 *	32 bits of pid, for consistency checking
 * If notepg, c->pgrpid.path is pgrp slot, .vers is noteid.
 */
#define QSHIFT	5	/* location in qid of proc slot # */

#define QID(q)		((((uint32_t)(q).path)&0x0000001F)>>0)
#define SLOT(q)		(((((uint32_t)(q).path)&0x07FFFFFE0)>>QSHIFT)-1)
#define PID(q)		((q).vers)
#define NOTEID(q)	((q).vers)

static void	procctlreq(Proc*, char*, int);
static int	procctlmemio(Proc*, uintptr_t, int, void*, int);
static Chan*	proctext(Chan*, Proc*);
static int	procstopped(void*);
static void	mntscan(Mntwalk*, Proc*);

static Traceevent *tevents;
static Lock tlock;
static int topens;
static int tproduced, tconsumed;

static void
profclock(Ureg *ur, Timer * _1)
{
	if(up == nil || up->state != Running)
		return;
}

static int
procgen(Chan *c, char *name, Dirtab *tab, int _1, int s, Dir *dp)
{
	Qid qid;
	Proc *p;
	char *ename;
	int pid;
	uint32_t path, perm, len;

	if(s == DEVDOTDOT){
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, "#p", 0, eve, 0555, dp);
		return 1;
	}

	if(c->qid.path == Qdir){
		if(s == 0){
			jehanne_strcpy(up->genbuf, "trace");
			mkqid(&qid, Qtrace, -1, QTFILE);
			devdir(c, qid, up->genbuf, 0, eve, 0444, dp);
			return 1;
		}

		if(name != nil){
			/* ignore s and use name to find pid */
			pid = jehanne_strtol(name, &ename, 10);
			if(pid<=0 || ename[0]!='\0')
				return -1;
			s = psindex(pid);
			if(s < 0)
				return -1;
		}
		else if(--s >= procalloc.nproc)
			return -1;

		if((p = psincref(s)) == nil || (pid = p->pid) == 0)
			return 0;
		jehanne_sprint(up->genbuf, "%d", pid);
		/*
		 * String comparison is done in devwalk so
		 * name must match its formatted pid.
		 */
		if(name != nil && jehanne_strcmp(name, up->genbuf) != 0)
			return -1;
		mkqid(&qid, (s+1)<<QSHIFT, pid, QTDIR);
		devdir(c, qid, up->genbuf, 0, p->user, DMDIR|0555, dp);
		psdecref(p);
		return 1;
	}
	if(c->qid.path == Qtrace){
		jehanne_strcpy(up->genbuf, "trace");
		mkqid(&qid, Qtrace, -1, QTFILE);
		devdir(c, qid, up->genbuf, 0, eve, 0444, dp);
		return 1;
	}
	if(s >= nelem(procdir))
		return -1;
	if(tab)
		panic("procgen");

	tab = &procdir[s];
	path = c->qid.path&~(((1<<QSHIFT)-1));	/* slot component */

	if((p = psincref(SLOT(c->qid))) == nil)
		return -1;
	perm = tab->perm;
	if(perm == 0)
		perm = p->procmode;
	else	/* just copy read bits */
		perm |= p->procmode & 0444;

	len = tab->length;
	switch(QID(c->qid)) {
	case Qwait:
		len = p->nwait;	/* incorrect size, but >0 means there's something to read */
		break;
	}
	switch(QID(tab->qid)){
	case Qwdir:
		/* file length might be relevant to the caller to
		 * malloc enough space in the buffer
		 */
		if(p->dot)
			len = 1 + jehanne_strlen(p->dot->path->s);
		break;
	}

	mkqid(&qid, path|tab->qid.path, c->qid.vers, QTFILE);
	devdir(c, qid, tab->name, len, p->user, perm, dp);
	psdecref(p);
	return 1;
}

static void
_proctrace(Proc* p, int etype, int64_t ts, int64_t _1)
{
	Traceevent *te;

	if (p->trace == 0 || topens == 0 ||
		tproduced - tconsumed >= Nevents)
		return;

	te = &tevents[tproduced&Emask];
	te->pid = p->pid;
	te->etype = (Tevent)etype;
	if (ts == 0)
		te->time = todget(nil);
	else
		te->time = ts;
	tproduced++;
}

static void
procinit(void)
{
	if(procalloc.nproc >= (1<<(16-QSHIFT))-1)
		jehanne_print("warning: too many procs for devproc\n");
	addclock0link((void (*)(void))profclock, 113);	/* Relative prime to HZ */
}

static Chan*
procattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('p', spec);
}

static Walkqid*
procwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, procgen);
}

static long
procstat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, 0, 0, procgen);
}

/*
 *  none can't read or write state on other
 *  processes.  This is to contain access of
 *  servers running as none should they be
 *  subverted by, for example, a stack attack.
 */
static void
nonone(Proc *p)
{
	if(p == up)
		return;
	if(jehanne_strcmp(up->user, "none") != 0)
		return;
	if(isevegroup())
		return;
	error(Eperm);
}

static Chan*
procopen(Chan *c, unsigned long omode)
{
	Proc *p;
	Pgrp *pg;
	Chan *tc;
	int pid;

	if(c->qid.type & QTDIR)
		return devopen(c, omode, 0, 0, procgen);

	if(QID(c->qid) == Qtrace){
		if (omode != OREAD)
			error(Eperm);
		lock(&tlock);
		if (waserror()){
			unlock(&tlock);
			nexterror();
		}
		if (topens > 0)
			error("already open");
		topens++;
		if (tevents == nil){
			tevents = (Traceevent*)jehanne_malloc(sizeof(Traceevent) * Nevents);
			if(tevents == nil)
				error(Enomem);
			tproduced = tconsumed = 0;
		}
		proctrace = _proctrace;
		poperror();
		unlock(&tlock);

		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	if((p = psincref(SLOT(c->qid))) == nil)
		error(Eprocdied);
	qlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		psdecref(p);
		nexterror();
	}
	pid = PID(c->qid);
	if(p->pid != pid)
		error(Eprocdied);

	omode = openmode(omode);

	switch(QID(c->qid)){
	case Qtext:
		if(omode != OREAD)
			error(Eperm);
		tc = proctext(c, p);
		tc->offset = 0;
		poperror();
		qunlock(&p->debug);
		psdecref(p);
		return tc;

	case Qproc:
	case Qppid:
	case Qkregs:
	case Qprofile:
	case Qfd:
		if(omode != OREAD && omode != OSTAT)
			error(Eperm);
		break;

	case Qsegment:
		if(omode != OREAD && omode != OSTAT)
			error(Eperm);
		break;

	case Qnote:
		if(p->privatemem)
			error(Eperm);
		if(p->state < Ready && omode != OREAD && omode != OSTAT)
			error(Eperm);
		break;

	case Qmem:
	case Qctl:
		if(p->privatemem)
			error(Eperm);
		nonone(p);
		break;

	case Qargs:
	case Qnoteid:
	case Qstatus:
	case Qwait:
	case Qregs:
	case Qfpregs:
		nonone(p);
		break;

	case Qsyscall:
		nonone(p);
		if(p->syscallq != nil)
			error(Einuse);
		c->aux = qopen(1024, 1, nil, nil);
		p->syscallq = c->aux;
		break;

	case Qns:
		if(omode == OREAD){
			c->aux = jehanne_malloc(sizeof(Mntwalk));
		} else if(omode == OWRITE){
			if(up->pgrp->noattach || strcmp(up->user, p->user) != 0)
				error(Eperm);
		} else {
			error(Eperm);
		}
		break;

	case Qnotepg:
		nonone(p);
		pg = p->pgrp;
		if(pg == nil)
			error(Eprocdied);
		if(omode!=OWRITE || pg->pgrpid == 1)
			error(Eperm);
		c->pgrpid.path = pg->pgrpid+1;
		c->pgrpid.vers = p->noteid;
		break;

	case Qwdir:
		if(p == up)	/* self write is always allowed */
			break;
		if(omode > ORDWR)
			error(Eperm);
		if(jehanne_strcmp(up->user, p->user) != 0	/* process owner can read/write */
		|| !iseve()				/* host owner can read */
		|| (omode&OWRITE) != 0)
			error(Eperm);
		break;

	default:
		poperror();
		qunlock(&p->debug);
		psdecref(p);
		pprint("procopen %#llux\n", c->qid.path);
		error(Egreg);
	}

	/* Affix pid to qid */
	if(p->state != Dead)
		c->qid.vers = p->pid;

	/* make sure the process slot didn't get reallocated while we were playing */
	coherence();
	if(p->pid != pid)
		error(Eprocdied);

	tc = devopen(c, omode, 0, 0, procgen);
	poperror();
	qunlock(&p->debug);
	psdecref(p);

	return tc;
}

static long
procwstat(Chan *c, uint8_t *db, long n)
{
	Proc *p;
	Dir *d;

	if(c->qid.type & QTDIR)
		error(Eperm);

	if(QID(c->qid) == Qtrace)
		return devwstat(c, db, n);

	if((p = psincref(SLOT(c->qid))) == nil)
		error(Eprocdied);
	nonone(p);
	d = nil;
	qlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		psdecref(p);
		jehanne_free(d);
		nexterror();
	}

	if(p->pid != PID(c->qid))
		error(Eprocdied);

	if(jehanne_strcmp(up->user, p->user) != 0 && jehanne_strcmp(up->user, eve) != 0)
		error(Eperm);

	d = smalloc(sizeof(Dir)+n);
	n = jehanne_convM2D(db, n, &d[0], (char*)&d[1]);
	if(n == 0)
		error(Eshortstat);
	if(!emptystr(d->uid) && jehanne_strcmp(d->uid, p->user) != 0){
		if(jehanne_strcmp(up->user, eve) != 0)
			error(Eperm);
		else
			kstrdup(&p->user, d->uid);
	}
	if(d->mode != (uint32_t)~0UL)
		p->procmode = d->mode&0777;

	poperror();
	qunlock(&p->debug);
	psdecref(p);
	jehanne_free(d);

	return n;
}


static long
procoffset(long offset, char *va, int *np)
{
	if(offset > 0) {
		offset -= *np;
		if(offset < 0) {
			jehanne_memmove(va, va+*np+offset, -offset);
			*np = -offset;
		}
		else
			*np = 0;
	}
	return offset;
}

static int
procqidwidth(Chan *c)
{
	char buf[32];

	return jehanne_sprint(buf, "%lud", c->qid.vers);
}


static int
_procfdprint(Chan *c, int fd, int w, char *s, int ns, char *modestr)
{
	int n;
	char *flags;
	if((c->flag&(OCEXEC|ORCLOSE)) == (OCEXEC|ORCLOSE))
		flags = "ED";
	else if(c->flag&OCEXEC)
		flags = "E ";
	else if(c->flag&ORCLOSE)
		flags = "D ";
	else
		flags = "  ";

	if(w == 0)
		w = procqidwidth(c);
	n = jehanne_snprint(s, ns, "%3d %.2s%s %C %4ud (%.16llux %*lud %.2ux) %5ld %8lld %s\n",
		fd,
		&modestr[(c->mode&3)<<1],
		flags,
		c->dev->dc, c->devno,
		c->qid.path, w, c->qid.vers, c->qid.type,
		c->iounit, c->offset, c->path->s);
	return n;
}

int
procfdprint(Chan *c, int fd, int w, char *s, int ns)
{
	return _procfdprint(c, fd, w, s, ns, " s r wrw");
}

static int
procfds(Proc *p, char *va, int count, long offset)
{
	Fgrp *f;
	Chan *c;
	char buf[256];
	int n, i, w, ww;
	char *a, *modestr;

	/* print to buf to avoid holding fgrp lock while writing to user space */
	if(count > sizeof buf)
		count = sizeof buf;
	a = buf;

	qlock(&p->debug);
	f = p->fgrp;
	if(f == nil){
		qunlock(&p->debug);
		return 0;
	}
	lock(&f->l);
	if(waserror()){
		unlock(&f->l);
		qunlock(&p->debug);
		nexterror();
	}

	n = readstr(0, a, count, p->dot->path->s);
	n += jehanne_snprint(a+n, count-n, "\n");
	offset = procoffset(offset, a, &n);
	/* compute width of qid.path */
	w = 0;
	for(i = 0; i <= f->maxfd; i++) {
		c = f->fd[i];
		if(c == nil)
			continue;
		ww = procqidwidth(c);
		if(ww > w)
			w = ww;
	}
	for(i = 0; i <= f->maxfd; i++) {
		c = f->fd[i];
		if(c == nil)
			continue;
		modestr = " s r wrw";
		if(p->blockingfd == i){
			if(p->scallnr == SysPread)
				modestr = " s R wRw";
			else
				modestr = " s r WrW";
		}
		n += _procfdprint(c, i, w, a+n, count-n, modestr);
		offset = procoffset(offset, a, &n);
	}
	poperror();
	unlock(&f->l);
	qunlock(&p->debug);

	/* copy result to user space, now that locks are released */
	jehanne_memmove(va, buf, n);

	return n;
}

static void
procclose(Chan * c)
{
	Proc *p;

	switch(QID(c->qid)){
	case Qtrace:
		lock(&tlock);
		if(topens > 0)
			topens--;
		if(topens == 0)
			proctrace = nil;
		unlock(&tlock);
		break;
	case Qns:
		if(c->aux != nil)
			jehanne_free(c->aux);
		break;
	case Qsyscall:
		if((p = psincref(SLOT(c->qid))) != nil){
			qlock(&p->debug);
			if(p->pid == PID(c->qid))
				p->syscallq = nil;
			qunlock(&p->debug);
			psdecref(p);
		}
		if(c->aux != nil)
			qfree(c->aux);
		break;
	}
}

static void
int2flag(int flag, char *s)
{
	if(flag == 0){
		*s = '\0';
		return;
	}
	*s++ = '-';
	if(flag & MAFTER)
		*s++ = 'a';
	if(flag & MBEFORE)
		*s++ = 'b';
	if(flag & MCREATE)
		*s++ = 'c';
	*s = '\0';
}

static int
procargs(Proc *p, char *buf, int nbuf)
{
	int i;
	char **args, **margs, *e;
	ProcSegment *s;
	PagePointer page = 0;
	char *pbase, *marg;
	uintptr_t argaddr, ptop;

	args = p->args;
	if(args == nil)
		return 0;
	if(p->setargs){
		jehanne_snprint(buf, nbuf, "%s [%s]", p->text, args[0]);
		return jehanne_strlen(buf);
	}
	e = buf + nbuf;

	/* since we did not created a copy of args on exec, we have
	 * to do a little black magic
	 */
	rlock(&p->seglock);	/* enough to avoid problems since
				 * the stack is never shared
				 */

	s = p->seg[SSEG];

	/* note that neither segment_page nor page_kmap increase the
	 * page's ref count, but this should be safe in this very specific
	 * case (top of the stack with seglock rlocked)
	 */
	page = segment_page(s, (uintptr_t)args);
	if(page == 0){
		buf = jehanne_seprint(buf, e, "cannot print args for %s %d: stack gone", p->text, p->pid);
		goto ArgsPrinted;
	}

	pbase = page_kmap(page);
	ptop = (uintptr_t)(pbase + PGSZ);

	margs = (char**)(pbase + (((uintptr_t)args)&(PGSZ-1)));
	if(((uintptr_t)&margs[p->nargs - 1]) > ptop){
		buf = jehanne_seprint(buf, e, "%s: too many arguments", p->text);
		goto DoneWithMappedPage;
	}
	for(i = 0; i < p->nargs; ++i){
		if(buf >= e)
			break;
		argaddr = (uintptr_t)margs[i];
		if(argaddr < (uintptr_t)args || argaddr >= s->top){
			buf = jehanne_seprint(buf, e, i?" %#p":"%#p", argaddr);
		} else if(argaddr - (uintptr_t)args > PGSZ){
			buf = jehanne_seprint(buf, e, " ...");
			break;
		} else {
			marg = pbase + (argaddr&(PGSZ-1));
			buf = jehanne_seprint(buf, e, i?" %q":"%q", marg);
		}
	}
DoneWithMappedPage:
	page_kunmap(page, &pbase);

ArgsPrinted:
	runlock(&p->seglock);
	return buf - (e - nbuf);
}

static int
eventsavailable(void * _1)
{
	return tproduced > tconsumed;
}

static long
procread(Chan *c, void *va, long n, int64_t off)
{
	Proc *p;
	int64_t l;
	int32_t r;
	Waitq *wq;
	Ureg kur;
	uint8_t *rptr;
	Mntwalk *mw;
	ProcSegment *sg, *s;
	int i, j, navail, ne, pid, rsize;
	char flag[10],  *a, *sps, *srv, statbuf[NSEG*STATSIZE];
	uintptr_t offset;
	uintmem paddr, plimit, psize;
	uint64_t u;

	if(c->qid.type & QTDIR)
		return devdirread(c, va, n, 0, 0, procgen);

	a = va;
	offset = off;

	if(QID(c->qid) == Qtrace){
		if(!eventsavailable(nil))
			return 0;

		rptr = va;
		navail = tproduced - tconsumed;
		if(navail > n / sizeof(Traceevent))
			navail = n / sizeof(Traceevent);
		while(navail > 0) {
			if((tconsumed & Emask) + navail > Nevents)
				ne = Nevents - (tconsumed & Emask);
			else
				ne = navail;
			i = ne * sizeof(Traceevent);
			jehanne_memmove(rptr, &tevents[tconsumed & Emask], i);

			tconsumed += ne;
			rptr += i;
			navail -= ne;
		}
		return rptr - (uint8_t*)va;
	}
	if(QID(c->qid) == Qsyscall && qcanread(c->aux)){
		/* whatever the process status, the reader can read
		 * pending syscall records
		 */
		return qread(c->aux, va, n);
	}

	if((p = psincref(SLOT(c->qid))) == nil)
		error(Eprocdied);
	if(p->pid != PID(c->qid)){
		psdecref(p);
		error(Eprocdied);
	}

	switch(QID(c->qid)){
	default:
		psdecref(p);
		break;
	case Qargs:
		qlock(&p->debug);
		j = procargs(p, up->genbuf, sizeof up->genbuf);
		qunlock(&p->debug);
		psdecref(p);
		if(offset >= j)
			return 0;
		if(offset+n > j)
			n = j-offset;
		jehanne_memmove(va, &up->genbuf[offset], n);
		return n;

	case Qsyscall:
		psdecref(p);
		return qread(c->aux, va, n);

	case Qmem:
		if(!iskaddr(offset)
		|| (offset >= USTKTOP-USTKSIZE && offset < USTKTOP)){
			r = procctlmemio(p, offset, n, va, 1);
			psdecref(p);
			return r;
		}

		if(!iseve()){
			psdecref(p);
			error(Eperm);
		}

		/* validate kernel addresses */
		if(offset < PTR2UINT(end)) {
			if(offset+n > PTR2UINT(end))
				n = PTR2UINT(end) - offset;
			jehanne_memmove(va, UINT2PTR(offset), n);
			psdecref(p);
			return n;
		}
		paddr = PADDR(UINT2PTR(offset));
		if(!ismapped(&rmapram, paddr, &psize)){
			psdecref(p);
			error(Ebadarg);
		}
		plimit = paddr + psize;
		/* plimit-1 because plimit might be zero (address space top) */
		if(paddr+n >= plimit-1)
			n = plimit - paddr;
		jehanne_memmove(va, UINT2PTR(offset), n);
		psdecref(p);
		return n;

	case Qnote:
		qlock(&p->debug);
		if(waserror()){
			qunlock(&p->debug);
			psdecref(p);
			nexterror();
		}
		if(p->pid != PID(c->qid))
			error(Eprocdied);
		if(n < 1)	/* must accept at least the '\0' */
			error(Etoosmall);
		if(p->nnote == 0)
			n = 0;
		else {
			i = jehanne_strlen(p->note[0].msg) + 1;
			if(i < n)
				n = i;
			rptr = va;
			jehanne_memmove(rptr, p->note[0].msg, n);
			rptr[n-1] = '\0';
			if(--p->nnote == 0)
				p->notepending = 0;
			jehanne_memmove(p->note, p->note+1, p->nnote*sizeof(Note));
		}
		poperror();
		qunlock(&p->debug);
		psdecref(p);
		return n;

	case Qproc:
		if(offset >= sizeof(Proc)){
			psdecref(p);
			return 0;
		}
		if(offset+n > sizeof(Proc))
			n = sizeof(Proc) - offset;
		jehanne_memmove(va, ((char*)p)+offset, n);
		psdecref(p);
		return n;

	case Qregs:
		rptr = (uint8_t*)p->dbgreg;
		rsize = sizeof(Ureg);
		goto regread;

	case Qkregs:
		memset(&kur, 0, sizeof(Ureg));
		setkernur(&kur, p);
		rptr = (uint8_t*)&kur;
		rsize = sizeof(Ureg);
		goto regread;

	case Qfpregs:
		rptr = (uint8_t*)&p->fpsave;
		rsize = sizeof(FPsave);
	regread:
		if(rptr == nil){
			psdecref(p);
			error(Enoreg);
		}
		if(offset >= rsize){
			psdecref(p);
			return 0;
		}
		if(offset+n > rsize)
			n = rsize - offset;
		memmove(a, rptr+offset, n);
		psdecref(p);
		return n;

	case Qstatus:
		if(offset >= sizeof statbuf){
			psdecref(p);
			return 0;
		}
		if(offset+n > sizeof statbuf)
			n = (sizeof statbuf) - offset;

		sps = p->psstate;
		if(sps == 0)
			sps = statename[p->state];
		jehanne_memset(statbuf, ' ', sizeof statbuf);
		jehanne_sprint(statbuf, "%-*.*s%-*.*s%-12.11s",
			KNAMELEN, KNAMELEN-1, p->text,
			KNAMELEN, KNAMELEN-1, p->user,
			sps);
		j = 2*KNAMELEN + 12;

		for(i = 0; i < 6; i++) {
			l = p->time[i];
			if(i == TReal)
				l = sys->ticks - l;
			l = TK2MS(l);
			readnum(0, statbuf+j, NUMSIZE, l, NUMSIZE);
			j += NUMSIZE;
		}
		/* ignore stack, which is mostly non-existent */
		u = 0;
		for(i=1; i<NSEG; i++){
			s = p->seg[i];
			if(s)
				u += s->top - s->base;
		}
		readnum(0, statbuf+j, NUMSIZE, u>>10, NUMSIZE);
		j += NUMSIZE;
		readnum(0, statbuf+j, NUMSIZE, p->basepri, NUMSIZE);
		j += NUMSIZE;
		readnum(0, statbuf+j, NUMSIZE, p->priority, NUMSIZE);
		j += NUMSIZE;
		statbuf[j++] = '\n';
		if(offset+n > j)
			n = j-offset;
		jehanne_memmove(va, statbuf+offset, n);
		psdecref(p);
		return n;

	case Qsegment:
		j = 0;
		for(i = 0; i < NSEG; i++) {
			sg = p->seg[i];
			if(sg == 0)
				continue;
			j += jehanne_sprint(statbuf+j, "%-6s %c%c %p %p %4d\n",
				segment_name(sg),
				!(sg->type&SgWrite) ? 'R' : ' ',
				(sg->type&SgExecute) ? 'x' : ' ',
				' ', // sg->profile ? 'P' : ' ', // here was profiling
				sg->base, sg->top, sg->r.ref);
		}
		psdecref(p);
		if(offset >= j)
			return 0;
		if(offset+n > j)
			n = j-offset;
		if(n == 0 && offset == 0)
			exhausted("segments");
		jehanne_memmove(va, &statbuf[offset], n);
		return n;

	case Qwait:
		if(!canqlock(&p->qwaitr)){
			psdecref(p);
			error(Einuse);
		}

		if(waserror()) {
			qunlock(&p->qwaitr);
			psdecref(p);
			nexterror();
		}

		lock(&p->exl);
		if(up == p && p->nchild == 0 && p->waitq == 0) {
			unlock(&p->exl);
			error(Enochild);
		}
		pid = p->pid;
		while(p->waitq == 0) {
			unlock(&p->exl);
			sleep(&p->waitr, haswaitq, p);
			if(p->pid != pid)
				error(Eprocdied);
			lock(&p->exl);
		}
		wq = p->waitq;
		p->waitq = wq->next;
		p->nwait--;
		unlock(&p->exl);

		poperror();
		qunlock(&p->qwaitr);
		psdecref(p);
		n = jehanne_snprint(va, n, "%d %lud %lud %lud %q",
			wq->w.pid,
			wq->w.time[TUser], wq->w.time[TSys], wq->w.time[TReal],
			wq->w.msg);
		jehanne_free(wq);
		return n;

	case Qns:
		qlock(&p->debug);
		if(waserror()){
			qunlock(&p->debug);
			psdecref(p);
			nexterror();
		}
		if(p->pgrp == nil || p->pid != PID(c->qid))
			error(Eprocdied);
		mw = c->aux;
		if(mw->cddone){
			poperror();
			qunlock(&p->debug);
			psdecref(p);
			return 0;
		}
		mntscan(mw, p);
		if(mw->mh == 0){
			mw->cddone = 1;
			i = jehanne_snprint(va, n, "cd %s\n", p->dot->path->s);
			poperror();
			qunlock(&p->debug);
			psdecref(p);
			return i;
		}
		int2flag(mw->cm->mflag, flag);
		if(jehanne_strcmp(mw->cm->to->path->s, "#9") == 0){
			srv = srvname(mw->cm->to->mchan);
			i = jehanne_snprint(va, n, "mount %s %s %s %s\n", flag,
				srv==nil? mw->cm->to->mchan->path->s : srv,
				mw->mh->from->path->s, mw->cm->spec? mw->cm->spec : "");
			jehanne_free(srv);
		}else
			i = jehanne_snprint(va, n, "bind %s %s %s\n", flag,
				mw->cm->to->path->s, mw->mh->from->path->s);
		poperror();
		qunlock(&p->debug);
		psdecref(p);
		return i;

	case Qnoteid:
		r = readnum(offset, va, n, p->noteid, NUMSIZE);
		psdecref(p);
		return r;
	case Qppid:
		r = readnum(offset, va, n, p->parentpid, NUMSIZE);
		psdecref(p);
		return r;
	case Qfd:
		r = procfds(p, va, n, offset);
		psdecref(p);
		return r;
	case Qwdir:
		r = read_working_dir(p, va, n, off);
		psdecref(p);
		return r;
	}
	error(Egreg);
	return 0;			/* not reached */
}

static void
mntscan(Mntwalk *mw, Proc *p)
{
	Pgrp *pg;
	Mount *t;
	Mhead *f;
	int best, i, last, nxt;

	pg = p->pgrp;
	rlock(&pg->ns);

	nxt = 0;
	best = (int)(~0U>>1);		/* largest 2's complement int */

	last = 0;
	if(mw->mh)
		last = mw->cm->mountid;

	for(i = 0; i < MNTHASH; i++) {
		for(f = pg->mnthash[i]; f; f = f->hash) {
			for(t = f->mount; t; t = t->next) {
				if(mw->mh == 0 ||
				  (t->mountid > last && t->mountid < best)) {
					mw->cm = t;
					mw->mh = f;
					best = mw->cm->mountid;
					nxt = 1;
				}
			}
		}
	}
	if(nxt == 0)
		mw->mh = 0;

	runlock(&pg->ns);
}


static long
procwrite(Chan *c, void *va, long n, int64_t off)
{
	Proc *p, *t;
	Pgrp *opg;
	int i, id, l;
	char **args, buf[ERRMAX];
	uintptr_t offset;

	if(c->qid.type & QTDIR)
		error(Eisdir);

	/* Use the remembered noteid in the channel rather
	 * than the process pgrpid
	 */
	if(QID(c->qid) == Qnotepg) {
		pgrpnote(NOTEID(c->pgrpid), va, n, NUser);
		return n;
	}

	if((p = psincref(SLOT(c->qid))) == nil)
		error(Eprocdied);

	qlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		psdecref(p);
		nexterror();
	}
	if(p->pid != PID(c->qid))
		error(Eprocdied);

	offset = off;

	switch(QID(c->qid)){
	case Qargs:
		if(n == 0)
			error(Eshort);
		if(n >= ERRMAX)
			error(Etoobig);
		jehanne_memmove(buf, va, n);
		args = jehanne_malloc(sizeof(char*)+n+1);
		if(args == nil)
			error(Enomem);
		args[0] = ((char*)args)+sizeof(char*);
		jehanne_memmove(args[0], buf, n);
		l = n;
		if(args[0][l-1] != 0)
			args[0][l++] = 0;
		if(p->setargs)	/* setargs == 0 => args in stack from sysexec */
			jehanne_free(p->args);
		p->nargs = l;
		p->args = args;
		p->setargs = 1;
		break;

	case Qmem:
		if(p->state != Stopped)
			error(Ebadctl);

		n = procctlmemio(p, offset, n, va, 0);
		break;

	case Qregs:
		if(offset >= sizeof(Ureg))
			n = 0;
		else if(offset+n > sizeof(Ureg))
			n = sizeof(Ureg) - offset;
		if(p->dbgreg == nil)
			error(Enoreg);
		setregisters(p->dbgreg, (char*)(p->dbgreg)+offset, va, n);
		break;

	case Qfpregs:
		if(offset >= sizeof(FPsave))
			n = 0;
		else if(offset+n > sizeof(FPsave))
			n = sizeof(FPsave) - offset;
		memmove((uint8_t*)&p->fpsave+offset, va, n);
		break;

	case Qctl:
		procctlreq(p, va, n);
		break;

	case Qnote:
		if(p->kp)
			error(Eperm);
		if(n >= ERRMAX-1)
			error(Etoobig);
		jehanne_memmove(buf, va, n);
		buf[n] = 0;
		if(!postnote(p, 0, buf, NUser))
			error("note not posted");
		break;
	case Qnoteid:
		id = jehanne_atoi(va);
		if(id == p->pid) {
			p->noteid = id;
			break;
		}
		for(i = 0; (t = psincref(i)) != nil; i++){
			if(t->state == Dead || t->noteid != id){
				psdecref(t);
				continue;
			}
			if(jehanne_strcmp(p->user, t->user) != 0){
				psdecref(t);
				error(Eperm);
			}
			psdecref(t);
			p->noteid = id;
			break;
		}
		if(p->noteid != id)
			error(Ebadarg);
		break;
	case Qwdir:
		n = write_working_dir(p, va, n, off);
		break;
	case Qns:
		if(strcmp("clone", va) != 0)
			error(Ebadarg);
		opg = up->pgrp;
		up->pgrp = newpgrp();
		pgrpcpy(up->pgrp, p->pgrp);
		/* inherit noattach */
		up->pgrp->noattach = p->pgrp->noattach;
		/* inherit dot */
		if(up->dot != nil)
			cclose(up->dot);
		up->dot = p->dot;
		incref(&up->dot->r);
		closepgrp(opg);
		break;

	default:
		poperror();
		qunlock(&p->debug);
		psdecref(p);
		pprint("unknown qid %#llux in procwrite\n", c->qid.path);
		error(Egreg);
	}
	poperror();
	qunlock(&p->debug);
	psdecref(p);
	return n;
}

static void
procremove(Chan* c)
{
	Proc *p;
	int pid;

	if((p = psincref(SLOT(c->qid))) == nil)
		error(Eprocdied);
	qlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		psdecref(p);
		nexterror();
	}
	pid = PID(c->qid);
	if(p->pid != pid)
		error(Eprocdied);

	switch(QID(c->qid)){
	case Qppid:
		errorl(nil, p->parentpid);
		break;
	case Qnoteid:
		errorl(nil, p->noteid);
		break;
	default:
		error(Eperm);
	}
}

Dev procdevtab = {
	'p',
	"proc",

	devreset,
	procinit,
	devshutdown,
	procattach,
	procwalk,
	procstat,
	procopen,
	devcreate,
	procclose,
	procread,
	devbread,
	procwrite,
	devbwrite,
	procremove,
	procwstat,
};

static Chan*
proctext(Chan *c, Proc *p)
{
	Chan *tc;
	ProcSegment *s;

	if(p->state==Dead)
		error(Eprocdied);

	rlock(&p->seglock);
	s = p->seg[TSEG];
	if(s == 0){
		runlock(&p->seglock);
		error(Enonexist);
	}

	tc = image_chan(s->image);

	runlock(&p->seglock);

	if(tc == nil)
		error(Eprocdied);
	if(p->pid != PID(c->qid))
		error(Eprocdied);

	return tc;
}

void
procstopwait(Proc *p, int ctl)
{
	int pid;

	if(p->pdbg)
		error(Einuse);
	if(procstopped(p) || p->state == Broken)
		return;
	pid = p->pid;
	if(pid == 0)
		error(Eprocdied);
	if(ctl != 0)
		p->procctl = ctl;
	if(p == up)
		return;
	p->pdbg = up;
	qunlock(&p->debug);
	up->psstate = "Stopwait";
	if(waserror()) {
		qlock(&p->debug);
		p->pdbg = 0;
		nexterror();
	}
	sleep(&up->sleep, procstopped, p);
	poperror();
	qlock(&p->debug);
	if(p->pid != pid)
		error(Eprocdied);
}

static void
procctlcloseone(Proc *p, Fgrp *f, int fd)
{
	Chan *c;

	c = f->fd[fd];
	if(c == nil)
		return;
	f->fd[fd] = nil;
	unlock(&f->l);
	qunlock(&p->debug);
	cclose(c);
	qlock(&p->debug);
	lock(&f->l);
}

void
procctlclosefiles(Proc *p, int all, int fd)
{
	int i;
	Fgrp *f;

	f = p->fgrp;
	if(f == nil)
		error(Eprocdied);

	lock(&f->l);
	incref(&f->r);
	if(all)
		for(i = 0; i < f->maxfd; i++)
			procctlcloseone(p, f, i);
	else
		procctlcloseone(p, f, fd);
	unlock(&f->l);
	closefgrp(f);
}

static void
procctlreq(Proc *p, char *va, int n)
{
	int pri;
	Cmdbuf *cb;
	Cmdtab *ct;

	if(p->kp)	/* no ctl requests to kprocs */
		error(Eperm);

	cb = parsecmd(va, n);
	if(waserror()){
		jehanne_free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, proccmd, nelem(proccmd));

	switch(ct->index){
	case CMclose:
		procctlclosefiles(p, 0, jehanne_atoi(cb->f[1]));
		break;
	case CMclosefiles:
		procctlclosefiles(p, 1, 0);
		break;
	case CMhang:
		p->hang = 1;
		break;
	case CMkill:
		prockill(p, Proc_exitme, "sys: killed");
		break;
	case CMnohang:
		p->hang = 0;
		break;
	case CMnoswap:
		/* obsolete */
		break;
	case CMpri:
		pri = jehanne_atoi(cb->f[1]);
		if(pri > PriNormal && !iseve())
			error(Eperm);
		procpriority(p, pri, 0);
		break;
	case CMfixedpri:
		pri = jehanne_atoi(cb->f[1]);
		if(pri > PriNormal && !iseve())
			error(Eperm);
		procpriority(p, pri, 1);
		break;
	case CMprivate:
		p->privatemem = 1;
		break;
	case CMstart:
		if(p->state != Stopped)
			error(Ebadctl);
		ready(p);
		break;
	case CMstartstop:
		if(p->state != Stopped)
			error(Ebadctl);
		p->procctl = Proc_traceme;
		ready(p);
		procstopwait(p, Proc_traceme);
		break;
	case CMstartsyscall:
		if(p->state != Stopped)
			error(Ebadctl);
		p->procctl = Proc_tracesyscall;
		ready(p);
		procstopwait(p, Proc_tracesyscall);
		break;
	case CMstop:
		procstopwait(p, Proc_stopme);
		break;
	case CMwaitstop:
		procstopwait(p, 0);
		break;
	case CMwired:
		procwired(p, jehanne_atoi(cb->f[1]));
		break;
	case CMtrace:
		switch(cb->nf){
		case 1:
			p->trace ^= 1;
			break;
		case 2:
			p->trace = (jehanne_atoi(cb->f[1]) != 0);
			break;
		default:
			error("args");
		}
		break;
	}

	poperror();
	jehanne_free(cb);
}

static int
procstopped(void *a)
{
	Proc *p = a;
	return p->state == Stopped;
}

static int
procctlmemio(Proc *p, uintptr_t offset, int n, void *va, int read)
{
	char *k;
	PagePointer page;
	ProcSegment *s;
	uint8_t *b;
	uintptr_t l, mmuphys = 0;

	s = proc_segment(p, offset);
	if(s == nil)
		error(Ebadarg);

	if(offset+n >= s->top)
		n = s->top-offset;

	if(!segment_fault(&mmuphys, &offset, s, read ? FaultRead : FaultWrite))
		error(Ebadarg);

	page = segment_page(s, (uintptr_t)offset);
	if(page == 0)
		error(Ebadarg);

	k = page_kmap(page);
	l = PGSZ - (offset&(PGSZ-1));
	if(n > l)
		n = l;

	if(waserror()) {
		page_kunmap(page, &k);
		nexterror();
	}
	b = (uint8_t*)k;
	b += offset&(PGSZ-1);
	if(read == 1){
		/* caller is reading: the destination buffer is at va
		 * and it can fault if not yet loaded
		 */
		jehanne_memmove(va, b, n);
	} else {
		/* caller is writing: the source buffer is at va */
		jehanne_memmove(b, va, n);
	}
	poperror();
	page_kunmap(page, &k);

	if(read == 0)
		p->newtlb = 1;

	return n;
}
