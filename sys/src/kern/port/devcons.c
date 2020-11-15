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

#include	<authsrv.h>

enum
{
	Nconsdevs	= 64,		/* max number of consoles */

	/* Consdev flags */
	Ciprint		= 2,		/* call this fn from iprint */
	Cntorn		= 4,		/* change \n to \r\n */
};

typedef struct Consdev Consdev;

struct Consdev
{
	Chan*	c;			/* external file */
	Queue*	q;			/* i/o queue, if any */
	void	(*fn)(char*, int);	/* i/o function when no queue */
	int	flags;
};

void	(*consdebug)(void) = nil;
void	(*screenputs)(char*, int) = nil;

static void kmesgputs(char *, int);
static void kprintputs(char*, int);

static	Lock	consdevslock;
static	int	nconsdevs = 3;
static	Consdev	consdevs[Nconsdevs] =			/* keep this order */
{
	{nil, nil,	kmesgputs,	0},			/* kmesg */
	{nil, nil,	kprintputs,	Ciprint},		/* kprint */
	{nil, nil,	uartputs,	Ciprint|Cntorn},	/* serial */
};

static	int	nkbdqs;
static	int	nkbdprocs;
static	Queue*	kbdqs[Nconsdevs];
static	int	kbdprocs[Nconsdevs];
static	Queue*	kbdq;		/* unprocessed console input */
static	Queue*	lineq;		/* processed console input */
	Queue*	serialoq;	/* serial console output */
static	Queue*	kprintoq;	/* console output, for /dev/kprint */
static	uint32_t	kprintinuse;	/* test and set whether /dev/kprint is open */

int	panicking;

static struct
{
	QLock;

	int	raw;		/* true if we shouldn't process input */
	Ref	ctl;		/* number of opens to the control file */
	int	x;		/* index into line */
	char	line[1024];	/* current input line */

	int	count;
	int	ctlpoff;

	/*
	 * A place to save up characters at interrupt time
	 * before dumping them in the queue
	 */
	Lock	lockputc;
	char	istage[1024];
	char*	iw;
	char*	ir;
	char*	ie;
} kbd = {
	.iw	= kbd.istage,
	.ir	= kbd.istage,
	.ie	= kbd.istage + sizeof(kbd.istage),
};

char	*sysname;
int64_t	fasthz;

static void	seedrand(void);
static int	readtime(uint32_t, char*, int);
static int	readbintime(char*, int);
static int	writetime(char*, int);
static int	writebintime(char*, int);

enum
{
	CMhalt,
	CMreboot,
	CMpanic,
};

Cmdtab rebootmsg[] =
{
	CMhalt,		"halt",		1,
	CMreboot,	"reboot",	0,
	CMpanic,	"panic",	0,
};

/* To keep the rest of the kernel unware of new consdevs for now */
static void
kprintputs(char *s, int n)
{
	if(screenputs != nil)
		screenputs(s, n);
}

int
addconsdev(Queue *q, void (*fn)(char*,int), int i, int flags)
{
	Consdev *c;

	ilock(&consdevslock);
	if(i < 0)
		i = nconsdevs;
	else
		flags |= consdevs[i].flags;
	if(nconsdevs == Nconsdevs)
		panic("Nconsdevs too small");
	c = &consdevs[i];
	c->flags = flags;
	c->q = q;
	c->fn = fn;
	if(i == nconsdevs)
		nconsdevs++;
	iunlock(&consdevslock);
	return i;
}

void
delconsdevs(void)
{
	nconsdevs = 2;	/* throw away serial consoles and kprint */
	consdevs[1].q = nil;
}

static void
conskbdqproc(void *a)
{
	char buf[64];
	Queue *q;
	int nr;

	q = a;
	while((nr = qread(q, buf, sizeof(buf))) > 0)
		qwrite(kbdq, buf, nr);
	pexit("hangup", 1);
}

static void
kickkbdq(void)
{
	int i;

	if(up != nil && nkbdqs > 1 && nkbdprocs != nkbdqs){
		lock(&consdevslock);
		if(nkbdprocs == nkbdqs){
			unlock(&consdevslock);
			return;
		}
		for(i = 0; i < nkbdqs; i++)
			if(kbdprocs[i] == 0){
				kbdprocs[i] = 1;
				kproc("conskbdq", conskbdqproc, kbdqs[i]);
			}
		unlock(&consdevslock);
	}
}

int
addkbdq(Queue *q, int i)
{
	int n;

	ilock(&consdevslock);
	if(i < 0)
		i = nkbdqs++;
	if(nkbdqs == Nconsdevs)
		panic("Nconsdevs too small");
	kbdqs[i] = q;
	n = nkbdqs;
	iunlock(&consdevslock);
	switch(n){
	case 1:
		/* if there's just one, pull directly from it. */
		kbdq = q;
		break;
	case 2:
		/* later we'll merge bytes from all kbdqs into a single kbdq */
		kbdq = qopen(4*1024, 0, 0, 0);
		if(kbdq == nil)
			panic("no kbdq");
		/* fall through */
	default:
		kickkbdq();
	}
	return i;
}

void
printinit(void)
{
	lineq = qopen(2*1024, 0, nil, nil);
	if(lineq == nil)
		panic("printinit");
	qnoblock(lineq, 1);
}

int
consactive(void)
{
	int i;
	Queue *q;

	for(i = 0; i < nconsdevs; i++)
		if((q = consdevs[i].q) != nil && qlen(q) > 0)
			return 1;
	return 0;
}

void
prflush(void)
{
	unsigned long now;

	now = sys->ticks;
	while(consactive())
		if(sys->ticks - now >= 30*HZ)
			break;
}

/*
 * Log console output so it can be retrieved via /dev/kmesg.
 * This is good for catching boot-time messages after the fact.
 */
struct {
	Lock lk;
	char buf[16384];
	uint32_t n;
} kmesg;

static void
kmesgputs(char *str, int n)
{
	uint32_t nn, d;

	ilock(&kmesg.lk);
	/* take the tail of huge writes */
	if(n > sizeof kmesg.buf){
		d = n - sizeof kmesg.buf;
		str += d;
		n -= d;
	}

	/* slide the buffer down to make room */
	nn = kmesg.n;
	if(nn + n >= sizeof kmesg.buf){
		d = nn + n - sizeof kmesg.buf;
		if(d)
			jehanne_memmove(kmesg.buf, kmesg.buf+d, sizeof kmesg.buf-d);
		nn -= d;
	}

	/* copy the data in */
	jehanne_memmove(kmesg.buf+nn, str, n);
	nn += n;
	kmesg.n = nn;
	iunlock(&kmesg.lk);
}

static void
consdevputs(Consdev *c, char *s, int n, int usewrite)
{
	Chan *cc;
	Queue *q;

	if((cc = c->c) != nil && usewrite)
		cc->dev->write(cc, s, n, 0);
	else if((q = c->q) != nil && !qisclosed(q))
		if(usewrite)
			qwrite(q, s, n);
		else
			qiwrite(q, s, n);
	else if(c->fn != nil)
		c->fn(s, n);
}

/*
 *   Print a string on the console.  Convert \n to \r\n for serial
 *   line consoles.  Locking of the queues is left up to the screen
 *   or uart code.  Multi-line messages to serial consoles may get
 *   interspersed with other messages.
 */
static void
putstrn0(char *str, int n, int usewrite)
{
	Consdev *c;
	char *s, *t;
	int i, len, m;

	if(!islo())
		usewrite = 0;

	for(i = 0; i < nconsdevs; i++){
		c = &consdevs[i];
		len = n;
		s = str;
		while(len > 0){
			t = nil;
			if((c->flags&Cntorn) && !kbd.raw)
				t = jehanne_memchr(s, '\n', len);
			if(t != nil && !kbd.raw){
				m = t-s;
				consdevputs(c, s, m, usewrite);
				consdevputs(c, "\r\n", 2, usewrite);
				len -= m+1;
				s = t+1;
			}else{
				consdevputs(c, s, len, usewrite);
				break;
			}
		}
	}
}

void
putstrn(char *str, int n)
{
	putstrn0(str, n, 0);
}

int
jehanne_print(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = jehanne_vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	putstrn(buf, n);

	return n;
}

/*
 * Want to interlock iprints to avoid interlaced output on
 * multiprocessor, but don't want to deadlock if one processor
 * dies during print and another has something important to say.
 * Make a good faith effort.
 */
static Lock iprintlock;

static int
iprintcanlock(Lock *l)
{
	int i;

	for(i=0; i<1000; i++){
		if(canlock(l))
			return 1;
		if(ownlock(l))
			return 0;
		microdelay(100);
	}
	return 0;
}

int
iprint(char *fmt, ...)
{
	Mreg s;
	int i, n, locked; //, mlocked;
	va_list arg;
	char buf[PRINTSIZE];

	s = splhi();
	va_start(arg, fmt);
	n = jehanne_vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
//	mlocked = malloclocked();
	locked = iprintcanlock(&iprintlock);
	for(i = 0; i < nconsdevs; i++){
		if((consdevs[i].flags&Ciprint) != 0){
//			if(consdevs[i].q != nil && !mlocked)
//				qiwrite(consdevs[i].q, buf, n);
//			else
				consdevs[i].fn(buf, n);
		}
	}
	if(locked)
		unlock(&iprintlock);
	splx(s);

	return n;
}

#pragma profile 0
void
panic(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	if(panicking)
		exit(1);

	panicking = 1;

//	if(malloclocked())
//		consdevs[2].q = nil;	/* force direct uart output */
	consdevs[1].q = nil;	/* don't try to write to /dev/kprint */

	splhi();
	jehanne_strcpy(buf, "panic: ");
	va_start(arg, fmt);
	n = jehanne_vseprint(buf+jehanne_strlen(buf), buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	iprint("%s\n", buf);
	if(consdebug)
		(*consdebug)();
	prflush();
//	splx(s);
	buf[n] = '\n';
	putstrn(buf, n+1);
//	dumpstack();
//	prflush();
//	dump();

	exit(1);
}

#pragma profile 1
/* libmp at least contains a few calls to sysfatal; simulate with panic */
void
jehanne_sysfatal(char *fmt, ...)
{
	char err[256];
	va_list arg;

	va_start(arg, fmt);
	jehanne_vseprint(err, err + sizeof err, fmt, arg);
	va_end(arg);
	panic("sysfatal: %s", err);
}

void
jehanne__assert(const char *fmt)
{
	panic("assert failed at %#p: %s", getcallerpc(), fmt);
}

int
pprint(char *fmt, ...)
{
	int n;
	Chan *c;
	va_list arg;
	char buf[2*PRINTSIZE];

	if(up == nil || up->fgrp == nil)
		return 0;

	c = up->fgrp->fd[2];
	if(c==0 || (c->mode!=OWRITE && c->mode!=ORDWR))
		return 0;
	n = jehanne_snprint(buf, sizeof buf, "%s %d: ", up->text, up->pid);
	va_start(arg, fmt);
	n = jehanne_vseprint(buf+n, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	if(waserror())
		return 0;
	c->dev->write(c, buf, n, c->offset);
	poperror();

	lock(&c->l);
	c->offset += n;
	unlock(&c->l);

	return n;
}

static void
echo(char *buf, int n)
{
	Mreg s;
	static int ctrlt;
	char *e, *p;

	if(n == 0)
		return;

	e = buf+n;
	for(p = buf; p < e; p++){
		switch(*p){
		case 0x10:	/* ^P */
			if(cpuserver && !kbd.ctlpoff){
				active.exiting = 1;
				return;
			}
			break;
		case 0x14:	/* ^T */
			ctrlt++;
			if(ctrlt > 2)
				ctrlt = 2;
			continue;
		}

		if(ctrlt != 2)
			continue;

		/* ^T escapes */
		ctrlt = 0;
		switch(*p){
		case 'S':
			s = splhi();
			dumpstack();
			procdump();
			splx(s);
			return;
		case 's':
			dumpstack();
			return;
		case 'x':
			mallocsummary();
			return;
		case 'd':
			if(consdebug == nil)
				consdebug = rdb;
			else
				consdebug = nil;
			jehanne_print("consdebug now %#p\n", consdebug);
			return;
		case 'D':
			if(consdebug == nil)
				consdebug = rdb;
			consdebug();
			return;
		case 'p':
			s = spllo();
			procdump();
			splx(s);
			return;
		case 'q':
			scheddump();
			return;
		case 'k':
			killbig("^t ^t k");
			return;
		case 'r':
			exit(0);
			return;
		}
	}

	if(kbdq != nil)
		qproduce(kbdq, buf, n);
}

/*
 *  Called by a uart interrupt for console input.
 *
 *  turn '\r' into '\n' before putting it into the queue.
 */
int
kbdcr2nl(Queue* _1, int ch)
{
	char *next;

	ilock(&kbd.lockputc);		/* just a mutex */
	if(ch == '\r' && !kbd.raw)
		ch = '\n';
	next = kbd.iw+1;
	if(next >= kbd.ie)
		next = kbd.istage;
	if(next != kbd.ir){
		*kbd.iw = ch;
		kbd.iw = next;
	}
	iunlock(&kbd.lockputc);
	return 0;
}

/*
 *  Put character, possibly a rune, into read queue at interrupt time.
 *  Called at interrupt time to process a character.
 */
int
kbdputc(Queue* _1, int ch)
{
	int i, n;
	char buf[UTFmax];
	Rune r;
	char *next;

	if(kbd.ir == nil)
		return 0;		/* in case we're not inited yet */

	ilock(&kbd.lockputc);		/* just a mutex */
	r = ch;
	n = jehanne_runetochar(buf, &r);
	for(i = 0; i < n; i++){
		next = kbd.iw+1;
		if(next >= kbd.ie)
			next = kbd.istage;
		if(next == kbd.ir)
			break;
		*kbd.iw = buf[i];
		kbd.iw = next;
	}
	iunlock(&kbd.lockputc);
	return 0;
}

/*
 *  we save up input characters till clock time to reduce
 *  per character interrupt overhead.
 */
static void
kbdputcclock(void)
{
	char *iw;

	/* this amortizes cost of qproduce */
	if(kbd.iw != kbd.ir){
		iw = kbd.iw;
		if(iw < kbd.ir){
			echo(kbd.ir, kbd.ie-kbd.ir);
			kbd.ir = kbd.istage;
		}
		if(kbd.ir != iw){
			echo(kbd.ir, iw-kbd.ir);
			kbd.ir = iw;
		}
	}
}

enum{
	Qdir,
	Qbintime,
//	Qconfig,
	Qcons,
	Qconsctl,
	Qcputime,
	Qdrivers,
	Qkmesg,
	Qkprint,
	Qhostdomain,
	Qhostowner,
	Qosversion,
	Qrandom,
	Qreboot,
	Qsysmem,
	Qsysname,
	Qsysstat,
	Qtime,
	Quser,
	Qusers,
};

enum
{
	VLNUMSIZE=	22,
};

static Dirtab consdir[]={
	".",	{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"bintime",	{Qbintime},	24,		0664,
//	"config",	{Qconfig},	0,		0444,
	"cons",		{Qcons},	0,		0660,
	"consctl",	{Qconsctl},	0,		0220,
	"cputime",	{Qcputime},	6*NUMSIZE,	0444,
	"drivers",	{Qdrivers},	0,		0444,
	"hostdomain",	{Qhostdomain},	DOMLEN,		0664,
	"hostowner",	{Qhostowner},	0,		0664,
	"kmesg",	{Qkmesg},	0,		0440,
	"kprint",	{Qkprint, 0, QTEXCL},	0,	DMEXCL|0440,
	"osversion",	{Qosversion},	0,		0444,
	"random",	{Qrandom},	0,		0444,
	"reboot",	{Qreboot},	0,		0664,
	"sysmem",	{Qsysmem},	0,		0664,
	"sysname",	{Qsysname},	0,		0664,
	"sysstat",	{Qsysstat},	0,		0666,
	"time",		{Qtime},	NUMSIZE+3*VLNUMSIZE,	0664,
	"user",		{Quser},	0,		0666,
	"users",		{Qusers},	0,		0644,
};

int
readnum(uint32_t off, char *buf, uint32_t n, uint32_t val, int size)
{
	char tmp[64];

	jehanne_snprint(tmp, sizeof(tmp), "%*lud", size-1, val);
	tmp[size-1] = ' ';
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	jehanne_memmove(buf, tmp+off, n);
	return n;
}

long
readstr(long offset, char *buf, long n, char *str)
{
	long size;

	size = jehanne_strlen(str);
	if(offset >= size)
		return 0;
	if(offset+n > size)
		n = size-offset;
	jehanne_memmove(buf, str+offset, n);
	return n;
}

static void
consinit(void)
{
	todinit();
	randominit();
	/*
	 * at 115200 baud, the 1024 char buffer takes 56 ms to process,
	 * processing it every 22 ms should be fine
	 */
	addclock0link(kbdputcclock, 22);
	kickkbdq();
}

static Chan*
consattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('c', spec);
}

static Walkqid*
conswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name,nname, consdir, nelem(consdir), devgen);
}

static long
consstat(Chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, consdir, nelem(consdir), devgen);
}

static Chan*
consopen(Chan *c, unsigned long omode)
{
	c->aux = nil;
	c = devopen(c, omode, consdir, nelem(consdir), devgen);
	switch((uint32_t)c->qid.path){
	case Qconsctl:
		incref(&kbd.ctl);
		break;

	case Qkprint:
		if(TAS(&kprintinuse) != 0){
			c->flag &= ~COPEN;
			error(Einuse);
		}
		if(kprintoq == nil){
			kprintoq = qopen(8*1024, Qcoalesce, 0, 0);
			if(kprintoq == nil){
				c->flag &= ~COPEN;
				error(Enomem);
			}
			qnoblock(kprintoq, 1);
			consdevs[1].q = kprintoq;
		}else
			qreopen(kprintoq);
		c->iounit = qiomaxatomic;
		break;
	}
	return c;
}

static void
consclose(Chan *c)
{
	switch((uint32_t)c->qid.path){
	/* last close of control file turns off raw */
	case Qconsctl:
		if(c->flag&COPEN){
			if(decref(&kbd.ctl) == 0)
				kbd.raw = 0;
		}
		break;

	/* close of kprint allows other opens */
	case Qkprint:
		if(c->flag & COPEN){
			kprintinuse = 0;
			qhangup(kprintoq, nil);
		}
		break;
	}
}

static long
consread(Chan *c, void *buf, long n, int64_t off)
{
	uint64_t l;
	Mach *mp;
	MemoryStats mstats;
	char *b, *bp, ch;
	char tmp[6*NUMSIZE+1];		/* must be >= 6*NUMSIZE (Qcputime) */
	int i, id, send;
	long offset;
//	extern char configfile[];

	if(n <= 0)
		return n;

	offset = off;
	switch((uint32_t)c->qid.path){
	case Qdir:
		return devdirread(c, buf, n, consdir, nelem(consdir), devgen);

	case Qcons:
		qlock(&kbd);
		if(waserror()) {
			qunlock(&kbd);
			nexterror();
		}
		while(!qcanread(lineq)){
			if(qread(kbdq, &ch, 1) == 0)
				continue;
			send = 0;
			if(ch == 0){
				/* flush output on rawoff -> rawon */
				if(kbd.x > 0)
					send = !qcanread(kbdq);
			}else if(kbd.raw){
				kbd.line[kbd.x++] = ch;
				send = !qcanread(kbdq);
			}else{
				switch(ch){
				case '\b':
					i = kbd.x;
					b = kbd.line;
					if(i > 0){
						i--;
						while(i > 0 && (b[i-1]&0xc0) == 0x80)
							i--;
						kbd.x = i;
					}
					break;
				case 0x15:	/* ^U */
					kbd.x = 0;
					jehanne_print("\b^U\n");
					break;
				case '\n':
				case 0x04:	/* ^D */
					send = 1;
					/* fall through */
				default:
					if(ch != 0x04)
						kbd.line[kbd.x++] = ch;
					break;
				}
			}
			if(send || kbd.x == sizeof kbd.line){
				qwrite(lineq, kbd.line, kbd.x);
				kbd.x = 0;
			}
		}
		n = qread(lineq, buf, n);
		qunlock(&kbd);
		poperror();
		return n;

	case Qcputime:
		/* easiest to format in a separate buffer and copy out */
		for(i=0; i<6; i++){
			l = up->time[i];
			if(i == TReal)
				l = sys->ticks - l;
			l = TK2MS(l);
			readnum(0, tmp+NUMSIZE*i, NUMSIZE, l, NUMSIZE);
		}
		tmp[sizeof(tmp)-1] = 0;
		return readstr(offset, buf, n, tmp);

	case Qkmesg:
		/*
		 * This is unlocked to avoid tying up a process
		 * that's writing to the buffer.  kmesg.n never
		 * gets smaller, so worst case the reader will
		 * see a slurred buffer.
		 */
		if(off >= kmesg.n)
			n = 0;
		else{
			if(off+n > kmesg.n)
				n = kmesg.n - off;
			jehanne_memmove(buf, kmesg.buf+off, n);
		}
		return n;

	case Qkprint:
		return qread(kprintoq, buf, n);

	case Qtime:
		return readtime(offset, buf, n);

	case Qbintime:
		return readbintime(buf, n);

	case Qhostowner:
		return readstr(offset, buf, n, eve);

	case Qhostdomain:
		return readstr(offset, buf, n, hostdomain);

	case Quser:
		return readstr(offset, buf, n, up->user);

	case Qusers:
		b = usersread();
		if(!waserror()){
			n = readstr(offset, buf, n, b);
			poperror();
		}
		jehanne_free(b);
		return n;

//	case Qconfig:
//		return readstr((uint32_t)offset, buf, n, configfile);

	case Qsysstat:
		b = smalloc(MACHMAX*(NUMSIZE*11+1) + 1);	/* +1 for NUL */
		bp = b;
		for(id = 0; id < MACHMAX; id++){
			if((mp = sys->machptr[id]) == nil || !mp->online)
				continue;
			readnum(0, bp, NUMSIZE, id, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->cs, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->intr, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->syscall, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->pfault, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->tlbfault, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, mp->tlbpurge, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE, sys->load, NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE,
				(mp->perf.avg_inidle*100)/mp->perf.period,
				NUMSIZE);
			bp += NUMSIZE;
			readnum(0, bp, NUMSIZE,
				(mp->perf.avg_inintr*100)/mp->perf.period,
				NUMSIZE);
			bp += NUMSIZE;
			*bp++ = '\n';
		}
		if(waserror()){
			jehanne_free(b);
			nexterror();
		}
		n = readstr(offset, buf, n, b);
		jehanne_free(b);
		poperror();
		return n;

	case Qsysmem:
		memory_stats(&mstats);
		jehanne_snprint(tmp, sizeof tmp,
			"%llud memory\n"
			"%llud pagesize\n"
			"%lud kernel\n"
			"%lud/%lud user\n",
			mstats.memory,
			(unsigned long)PGSZ,
			mstats.kernel,
			mstats.user, mstats.user_available);

		return readstr(offset, buf, n, tmp);

	case Qsysname:
		if(sysname == nil)
			return 0;
		return readstr(offset, buf, n, sysname);

	case Qrandom:
		return randomread(buf, n);

	case Qdrivers:
		return devtabread(c, buf, n, off);

	case Qosversion:
		return readstr(offset, buf, n, "2000");

	default:
		jehanne_print("consread %#llux\n", c->qid.path);
		error(Egreg);
	}
	return -1;		/* never reached */
}

static void
consremove(Chan* c)
{
	switch((uint32_t)c->qid.path){
	default:
		error(Eperm);
	case Qtime:
		errorl("fugit irreparabile tempus", todget(nil));
	}
}

static long
conswrite(Chan *c, void *va, long n, int64_t off)
{
	char buf[256], ch;
	long l, bp;
	char *a;
	Mach *mp;
	int i;
	uint32_t offset;
	Cmdbuf *cb;
	Cmdtab *ct;

	a = va;
	offset = off;

	switch((uint32_t)c->qid.path){
	case Qcons:
		/*
		 * Can't page fault in putstrn, so copy the data locally.
		 */
		l = n;
		while(l > 0){
			bp = l;
			if(bp > sizeof buf)
				bp = sizeof buf;
			jehanne_memmove(buf, a, bp);
			putstrn0(buf, bp, 1);
			a += bp;
			l -= bp;
		}
		break;

	case Qconsctl:
		if(n >= sizeof(buf))
			n = sizeof(buf)-1;
		jehanne_strncpy(buf, a, n);
		buf[n] = 0;
		for(a = buf; a;){
			if(jehanne_strncmp(a, "rawon", 5) == 0){
				kbd.raw = 1;
				/* clumsy hack - wake up reader */
				ch = 0;
				qwrite(kbdq, &ch, 1);
			}
			else if(jehanne_strncmp(a, "rawoff", 6) == 0)
				kbd.raw = 0;
			else if(jehanne_strncmp(a, "ctlpon", 6) == 0)
				kbd.ctlpoff = 0;
			else if(jehanne_strncmp(a, "ctlpoff", 7) == 0)
				kbd.ctlpoff = 1;
			if(a = jehanne_strchr(a, ' '))
				a++;
		}
		break;

	case Qtime:
		if(!iseve())
			error(Eperm);
		return writetime(a, n);

	case Qbintime:
		if(!iseve())
			error(Eperm);
		return writebintime(a, n);

	case Qhostowner:
		return hostownerwrite(a, n);

	case Qhostdomain:
		return hostdomainwrite(a, n);

	case Quser:
		return userwrite(a, n);

	case Qusers:
		return userswrite(a, n);

//	case Qconfig:
//		error(Eperm);
//		break;

	case Qreboot:
		if(!iseve())
			error(Eperm);
		cb = parsecmd(a, n);

		if(waserror()) {
			jehanne_free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, rebootmsg, nelem(rebootmsg));
		switch(ct->index) {
		case CMhalt:
			reboot(nil, 0, 0);
			break;
		case CMreboot:
			rebootcmd(cb->nf-1, cb->f+1);
			break;
		case CMpanic:
			*(uint32_t*)0=0;
			panic("/dev/reboot");
		}
		poperror();
		jehanne_free(cb);
		break;

	case Qsysstat:
		for(i = 0; i < MACHMAX; i++){
			if((mp = sys->machptr[i]) == nil || !mp->online)
				continue;
			mp->cs = 0;
			mp->intr = 0;
			mp->syscall = 0;
			mp->pfault = 0;
			mp->tlbfault = 0;
			mp->tlbpurge = 0;
		}
		break;

	case Qsysmem:
		/* no more */
		break;

	case Qsysname:
		if(offset != 0)
			error(Ebadarg);
		if(n <= 0 || n >= sizeof buf)
			error(Ebadarg);
		jehanne_strncpy(buf, a, n);
		buf[n] = 0;
		if(buf[n-1] == '\n')
			buf[n-1] = 0;
		kstrdup(&sysname, buf);
		break;

	default:
		jehanne_print("conswrite: %#llux\n", c->qid.path);
		error(Egreg);
	}
	return n;
}

Dev consdevtab = {
	'c',
	"cons",

	devreset,
	consinit,
	devshutdown,
	consattach,
	conswalk,
	consstat,
	consopen,
	devcreate,
	consclose,
	consread,
	devbread,
	conswrite,
	devbwrite,
	consremove,
	devwstat,
};

static	uint32_t	randn;

static void
seedrand(void)
{
	if(!waserror()){
		randomread((void*)&randn, sizeof(randn));
		poperror();
	}
}

int
nrand(int n)
{
	if(randn == 0)
		seedrand();
	randn = randn*1103515245 + 12345 + sys->ticks;
	return (randn>>16) % n;
}

int
rand(void)
{
	nrand(1);
	return randn;
}

static uint64_t uvorder = 0x0001020304050607ULL;

static uint8_t*
le2vlong(int64_t *to, uint8_t *f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t*)to;
	o = (uint8_t*)&uvorder;
	for(i = 0; i < sizeof(int64_t); i++)
		t[o[i]] = f[i];
	return f+sizeof(int64_t);
}

static uint8_t*
vlong2le(uint8_t *t, int64_t from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t*)&from;
	o = (uint8_t*)&uvorder;
	for(i = 0; i < sizeof(int64_t); i++)
		t[i] = f[o[i]];
	return t+sizeof(int64_t);
}

static long order = 0x00010203;

static uint8_t*
le2long(long *to, uint8_t *f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t*)to;
	o = (uint8_t*)&order;
	for(i = 0; i < sizeof(long); i++)
		t[o[i]] = f[i];
	return f+sizeof(long);
}

#if 0
static uint8_t*
long2le(uint8_t *t, long from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t*)&from;
	o = (uint8_t*)&order;
	for(i = 0; i < sizeof(long); i++)
		t[i] = f[o[i]];
	return t+sizeof(long);
}
#endif

char *Ebadtimectl = "bad time control";

/*
 *  like the old #c/time but with added info.  Return
 *
 *	secs	nanosecs	fastticks	fasthz
 */
static int
readtime(uint32_t off, char *buf, int n)
{
	int64_t nsec, ticks;
	long sec;
	char str[7*NUMSIZE];

	nsec = todget(&ticks);
	if(fasthz == 0LL)
		fastticks((uint64_t*)&fasthz);
	sec = nsec/1000000000ULL;
	jehanne_snprint(str, sizeof(str), "%*lud %*llud %*llud %*llud ",
		NUMSIZE-1, sec,
		VLNUMSIZE-1, nsec,
		VLNUMSIZE-1, ticks,
		VLNUMSIZE-1, fasthz);
	return readstr(off, buf, n, str);
}

/*
 *  set the time in seconds
 */
static int
writetime(char *buf, int n)
{
	char b[13];
	long i;
	int64_t now;

	if(n >= sizeof(b))
		error(Ebadtimectl);
	jehanne_strncpy(b, buf, n);
	b[n] = 0;
	i = jehanne_strtol(b, 0, 0);
	if(i <= 0)
		error(Ebadtimectl);
	now = i*1000000000LL;
	todset(now, 0, 0);
	return n;
}

/*
 *  read binary time info.  all numbers are little endian.
 *  ticks and nsec are syncronized.
 */
static int
readbintime(char *buf, int n)
{
	int i;
	int64_t nsec, ticks;
	uint8_t *b = (uint8_t*)buf;

	i = 0;
	if(fasthz == 0LL)
		fastticks((uint64_t*)&fasthz);
	nsec = todget(&ticks);
	if(n >= 3*sizeof(uint64_t)){
		vlong2le(b+2*sizeof(uint64_t), fasthz);
		i += sizeof(uint64_t);
	}
	if(n >= 2*sizeof(uint64_t)){
		vlong2le(b+sizeof(uint64_t), ticks);
		i += sizeof(uint64_t);
	}
	if(n >= 8){
		vlong2le(b, nsec);
		i += sizeof(int64_t);
	}
	return i;
}

/*
 *  set any of the following
 *	- time in nsec
 *	- nsec trim applied over some seconds
 *	- clock frequency
 */
static int
writebintime(char *buf, int n)
{
	uint8_t *p;
	int64_t delta;
	long period;

	n--;
	p = (uint8_t*)buf + 1;
	switch(*buf){
	case 'n':
		if(n < sizeof(int64_t))
			error(Ebadtimectl);
		le2vlong(&delta, p);
		todset(delta, 0, 0);
		break;
	case 'd':
		if(n < sizeof(int64_t)+sizeof(long))
			error(Ebadtimectl);
		p = le2vlong(&delta, p);
		le2long(&period, p);
		todset(-1, delta, period);
		break;
	case 'f':
		if(n < sizeof(uint64_t))
			error(Ebadtimectl);
		le2vlong(&fasthz, p);
		todsetfreq(fasthz);
		break;
	}
	return n;
}
