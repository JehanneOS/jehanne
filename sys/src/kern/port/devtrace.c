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
#include	"netif.h"

/*
 * NB: To be used with 6l -e so tracein/out are called upon
 * function entry and exit.
 * There's no trace(3) man page, look at write source to see the
 * commands.
 */

#pragma profile 0

typedef struct Trace Trace;
/* This is a trace--a segment of memory to watch for entries and exits */
struct Trace {
	struct Trace *next;
	void *func;
	void *start;
	void *end;
	int enabled;
	char name[16];
};

enum {
	Qdir,
	Qctl,
	Qdata,
};

enum {
	TraceEntry = 1,
	TraceExit,
};

/* fix me make this programmable */
enum {
	defaultlogsize = 8192,
};

/* This represents a trace "hit" or event */
typedef struct Tracelog Tracelog;
struct Tracelog {
	uint64_t ticks;
	int info;
	uintptr_t pc;
	/* these are different depending on type */
	uintptr_t dat[5];
	int machno;
};


//static Rendez tracesleep;	// not used
static QLock traceslock;
/* this will contain as many entries as there are valid pc values */
static Trace **tracemap;
static Trace *traces; /* This stores all the traces */
//static Lock loglk;	// not used
static Tracelog *tracelog = nil;
int traceactive = 0;
/* trace indices. These are just unsigned longs. You mask them
 * to get an index. This makes fifo empty/full etc. trivial.
 */
static uint32_t pw = 0, pr = 0;
static int tracesactive = 0;
static int all = 0;
static int watching = 0;
static int slothits = 0;
static unsigned int traceinhits = 0;
static unsigned int newplfail = 0;
static unsigned long logsize = defaultlogsize, logmask = defaultlogsize - 1;

static int printsize = 0;	//The length of a line being printed

/* These are for observing a single process */
static int *pidwatch = nil;
static int numpids = 0;
static const int PIDWATCHSIZE = 32; /* The number of PIDS that can be watched. Pretty arbitrary. */

int codesize = 0;

//static uint64_t lastestamp; /* last entry timestamp */ // not used
//static uint64_t lastxstamp; /* last exit timestamp */ // not used

/* Trace events can be either Entries or Exits */
static char eventname[] = {
	[TraceEntry] = 'E',
	[TraceExit] = 'X',
};

static Dirtab tracedir[]={
	".",		{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"tracectl",	{Qctl},		0,		0664,
	"trace",	{Qdata},	0,		0440,
};

char hex[] = {
	'0',
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7',
	'8',
	'9',
	'a',
	'b',
	'c',
	'd',
	'e',
	'f',
};

/* big-endian ... */
void
hex8(uint32_t l, char *c)
{
	int i;
	for(i = 2; i; i--){
		c[i-1] = hex[l&0xf];
		l >>= 4;
	}
}

void
hex16(uint32_t l, char *c)
{
	int i;
	for(i = 4; i; i--){
		c[i-1] = hex[l&0xf];
		l >>= 4;
	}
}

void
hex32(uint32_t l, char *c)
{
	int i;
	for(i = 8; i; i--){
		c[i-1] = hex[l&0xf];
		l >>= 4;
	}
}

void
hex64(uint64_t l, char *c)
{
	hex32(l>>32, c);
	hex32(l, &c[8]);
}

static int
lognonempty(void * _1)
{
	return pw - pr;
}

#if 0
static int
logfull(void)
{
	return (pw - pr) >= logsize;
}
#endif

static uint64_t
idx(uint64_t f)
{
	return f & logmask;
}

/*
 * Check if the given trace overlaps any others
 * Returns 1 if there is overlap, 0 if clear.
 */
int
overlapping(Trace *p) {
	Trace *curr;

	curr = traces;

	if (!curr)
		return 0;

	do {
		if ((curr->start < p->start && p->start < curr->end) ||
				(curr->start < p->end && p->end < curr->end))
			return 1;
		curr = curr->next;
	} while (curr != nil);

	return 0;
}

/* Make sure a PC is valid and traced; if so, return its Trace */
/* if dopanic == 1, the kernel will panic on an invalid PC */
struct Trace **
traceslot(void *pc, int dopanic)
{
	int index;
	struct Trace **p;

	if (pc > (void *)etext) {
		if (dopanic)
			panic("Bad PC %p", pc);

		jehanne_print("Invalid PC %p\n", pc);
		return nil;
	}
	index = (int)((uintptr_t)pc - KTZERO);
	if (index > codesize){
		if (dopanic) {
			panic("Bad PC %p", pc);
			while(1);
		}
		jehanne_print("Invalid PC %p\n", pc);
		return nil;
	}
	p = &tracemap[index];
	if (tracemap[index])
		ainc(&slothits);
	return p;
}

/* Check if the given PC is traced and return a Trace if so */
struct Trace *
traced(void *pc, int dopanic)
{
	struct Trace **p;

	p = traceslot(pc, dopanic);

	if (p == nil)
		return nil;

	return *p;
}

/*
 * Return 1 if pid is being watched or no pids are being watched.
 * Return 0 if pids are being watched and the argument is not
 * among them.
 */
int
watchingpid(int pid) {
	int i;

	if (pidwatch[0] == 0)
		return 1;

	for (i = 0; i < numpids; i++) {
		if (pidwatch[i] == pid)
			return 1;
	}
	return 0;
}

/*
 * Remove a trace.
 */
void
removetrace(Trace *p) {
	uint8_t *cp;
	struct Trace *prev;
	struct Trace *curr;
	struct Trace **slot;

	slot = traceslot(p->start, 0);
	for(cp = p->start; cp <= (uint8_t *)p->end; slot++, cp++)
		*slot = nil;

	curr = traces;

	if (curr == p) {
		if (curr->next) {
			traces = curr->next;
		} else {
			traces = nil;	//this seems to work fine
		}
		jehanne_free(curr);
		return;
	}

	prev = curr;
	curr = curr->next;
	do {
		if (curr == p) {
			prev->next = curr->next;
			return;
		}
		prev = curr;
		curr = curr->next;
	} while (curr != nil);

}

/* it is recommended that you call these with something sane. */
/* these next two functions assume you locked tracelock */

/* Turn on a trace */
void
traceon(struct Trace *p)
{
	uint8_t *cp;
	struct Trace **slot;
	slot = traceslot(p->start, 0);
	for(cp = p->start; cp <= (uint8_t *)p->end; slot++, cp++)
		*slot = p;
	p->enabled = 1;
	tracesactive++;
}

/* Turn off a trace */
void
traceoff(struct Trace  *p)
{
	uint8_t *cp;
	struct Trace **slot;
	slot = traceslot(p->start, 0);
	for(cp = p->start; cp <= (uint8_t *)p->end; slot++, cp++)
		*slot = nil;
	p->enabled = 0;
	tracesactive--;
}

/* Make a new tracelog (an event) */
/* can return NULL, meaning, no record for you */
static struct Tracelog *
newpl(void)
{
	uint32_t index;

	index = ainc((int *)&pw);

	return &tracelog[idx(index)];

}

/* Called every time a (traced) function starts */
/* this is not really smp safe. FIX */
void
tracein(void* pc, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	struct Tracelog *pl;

	/* if we are here, tracing is active. Turn it off. */
	traceactive = 0;
	if (! traced(pc, 1)){
		traceactive = 1;
		return;
	}

	ainc((int *)&traceinhits);
	/* Continue if we are watching this pid or we're not watching any */
	if (!all)
		if (!up || !watchingpid(up->pid)){
			traceactive = 1;
			return;
	}

	pl = newpl();

	if (! pl) {
		ainc((int *)&newplfail);
		traceactive = 1;
		return;
	}

	cycles(&pl->ticks);

	pl->pc = (uintptr_t)pc;
	if (up)
		pl->dat[0] = up->pid;
	else
		pl->dat[0] = (unsigned long)-1;

	pl->dat[1] = a1;
	pl->dat[2] = a2;
	pl->dat[3] = a3;
	pl->dat[4] = a4;

	pl->info = TraceEntry;
	pl->machno = m->machno;
	traceactive = 1;
}

/* Called every time a traced function exits */
void
traceout(void* pc, uintptr_t retval)
{
	struct Tracelog *pl;
	/* if we are here, tracing is active. Turn it off. */
	traceactive = 0;
	if (! traced(pc, 1)){
		traceactive = 1;
		return;
	}

	if (!all)
		if (!up || !watchingpid(up->pid)){
			traceactive = 1;
			return;
		}

	pl = newpl();
	if (! pl){
		traceactive = 1;
		return;
	}

	cycles(&pl->ticks);

	pl->pc = (uintptr_t)pc;
	if (up)
		pl->dat[0] = up->pid;
	else
		pl->dat[0] = (unsigned long)-1;

	pl->dat[1] = retval;
	pl->dat[2] = 0;
	pl->dat[3] = 0;

	pl->info = TraceExit;
	pl->machno = m->machno;
	traceactive = 1;
}

/* Create a new trace with the given range */
static Trace *
mktrace(void *func, void *start, void *end)
{
	Trace *p;
	p = jehanne_mallocz(sizeof p[0], 1);
	p->func = func;
	p->start = start;
	p->end = end;
	return p;
}

#if 0
/* Get rid of an old trace */
static void
freetrace(Trace *p)
{
	jehanne_free(p);
}
#endif

static Chan*
traceattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('T', spec);
}

static Walkqid*
tracewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, tracedir, nelem(tracedir), devgen);
}

static long
tracestat(Chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, tracedir, nelem(tracedir), devgen);
}

static Chan*
traceopen(Chan *c, unsigned long omode)
{

	/* if there is no tracelog, allocate one. Open always fails
	 * if the basic alloc fails. You can resize it later.
	 */

	codesize = (uintptr_t)etext - (uintptr_t)KTZERO;
	if (! tracemap)
		//tracemap = jehanne_mallocz(sizeof(struct tracemap *)*codesize, 1);
		tracemap = jehanne_mallocz(sizeof(struct Trace *)*codesize, 1);
	if (! tracemap)
		error("tracemap malloc failed");
	if (! tracelog)
		tracelog = jehanne_mallocz(sizeof(*tracelog)*logsize, 1);
	/* I guess malloc doesn't toss an error */
	if (! tracelog)
		error("tracelog malloc failed");
	if (! pidwatch)
		pidwatch = jehanne_mallocz(sizeof(int)*PIDWATCHSIZE, 1);
	if (! pidwatch)
		error("pidwatch malloc failed");
	c = devopen(c, omode, tracedir, nelem(tracedir), devgen);
	return c;
}

static void
traceclose(Chan * _1)
{
}

/*
 * Reading from the device, either the data or control files.
 * The data reading involves deep rminnich magic so we don't have
 * to call jehanne_print(), which is traced.
 */
static long
traceread(Chan *c, void *a, long n, int64_t offset)
{
	char *buf;
	char *cp = a;
	struct Tracelog *pl;
	Trace *p;
	int i, j;
	int saveactive = traceactive;
	traceactive = 0;
//	static QLock gate;	// not used

	if (waserror()) {
		traceactive = saveactive;
		nexterror();
	}

	if(c->qid.type == QTDIR) {
		long l = devdirread(c, a, n, tracedir, nelem(tracedir), devgen);
		poperror();
		traceactive = saveactive;
		return l;
	}

	switch((int) c->qid.path){
	default:
		error("traceread: bad qid");
	case Qctl:
		i = 0;
		qlock(&traceslock);
		buf = jehanne_malloc(READSTR);
		i += jehanne_snprint(buf + i, READSTR - i, "logsize %lud\n", logsize);
		for(p = traces; p != nil; p = p->next)
			i += jehanne_snprint(buf + i, READSTR - i, "trace %p %p new %s\n",
				p->start, p->end, p->name);

		for(p = traces; p != nil; p = p->next)
			i += jehanne_snprint(buf + i, READSTR - i, "#trace %p traced? %p\n",
				p->func, traced(p->func, 0));

		for(p = traces; p != nil; p = p->next)
			if (p->enabled)
				i += jehanne_snprint(buf + i, READSTR - i, "trace %s on\n",
				p->name);
		i += jehanne_snprint(buf + i, READSTR - i, "#tracehits %d, in queue %d\n",
				pw, pw-pr);
		i += jehanne_snprint(buf + i, READSTR - i, "#tracelog %p\n", tracelog);
		i += jehanne_snprint(buf + i, READSTR - i, "#traceactive %d\n", saveactive);
		i += jehanne_snprint(buf + i, READSTR - i, "#slothits %d\n", slothits);
		i += jehanne_snprint(buf + i, READSTR - i, "#traceinhits %d\n", traceinhits);
		for (j = 0; j < numpids - 1; j++)
			i += jehanne_snprint(buf + i, READSTR - i, "watch %d\n", pidwatch[j]);
		jehanne_snprint(buf + i, READSTR - i, "watch %d\n", pidwatch[numpids - 1]);
		n = readstr(offset, a, n, buf);
		jehanne_free(buf);
		qunlock(&traceslock);
		break;
	case Qdata:

		// Set the printsize
		/* 32-bit E PCPCPCPC TIMETIMETIMETIME PID# CR XXARG1XX XXARG2XX XXARG3XX XXARG4XX\n */
		if (sizeof(uintptr_t) == 4) {
			printsize = 73;		// 32-bit format
		} else {
			printsize = 121;	// must be 64-bit
		}

		i = 0;
		while(lognonempty((void *)0)){
			int j;

			if ((pw - pr) > logsize)
				pr = pw - logsize;

			pl = tracelog + idx(pr);

			if ((i + printsize) > n)
				break;
			/* simple format */
			if (sizeof(uintptr_t) == 4) {
				cp[0] = eventname[pl->info];
				cp ++;
				*cp++ = ' ';
				hex32((uint32_t)pl->pc, cp);
				cp[8] = ' ';
				cp += 9;
				hex64(pl->ticks, cp);
				cp[16] = ' ';
				cp += 17;
				hex16(pl->dat[0], cp);
				cp += 4;
				cp[0] = ' ';
				cp++;
				hex8(pl->machno, cp);
				cp += 2;
				cp[0] = ' ';
				cp++;
				for(j = 1; j < 4; j++){
					hex32(pl->dat[j], cp);
					cp[8] = ' ';
					cp += 9;
				}
				/* adjust for extra skip above */
				cp--;
				*cp++ = '\n';
				pr++;
				i += printsize;
			} else  {
				cp[0] = eventname[pl->info];
				cp ++;
				*cp++ = ' ';
				hex64((uint64_t)pl->pc, cp);
				cp[16] = ' ';
				cp += 17;
				hex64(pl->ticks, cp);
				cp[16] = ' ';
				cp += 17;
				hex32(pl->dat[0], cp);
				cp += 8;
				cp[0] = ' ';
				cp++;
				cp[0] = ' ';
				cp++;
				cp[0] = ' ';
				cp++;
				cp[0] = ' ';
				cp++;
				hex8(pl->machno, cp);
				cp += 4;
				for (j = 1; j < 5; j++) {
					hex64(pl->dat[j], cp);
					cp[16] = ' ';
					cp += 17;
				}
				cp--;
				*cp++ = '\n';
				pr++;
				i += printsize;
			}
		}
		n = i;
		break;
	}
	poperror();
	traceactive = saveactive;
	return n;
}

/*
 * Process commands sent to the ctl file.
 */
static long
tracewrite(Chan *c, void *a, long n, int64_t _1)
{
	char *tok[6]; //changed this so "tracein" works with the new 4th arg
	char *ep, *s = nil;
	Trace *p, **pp, *foo;
	int ntok;
	int saveactive = traceactive;
	traceactive = 0;

	qlock(&traceslock);
	if(waserror()){
		qunlock(&traceslock);
		if(s != nil) jehanne_free(s);
		traceactive = saveactive;
		nexterror();
	}
	switch((uintptr_t)c->qid.path){
	default:
		error("tracewrite: bad qid");
	case Qctl:
		s = jehanne_malloc(n + 1);
		jehanne_memmove(s, a, n);
		s[n] = 0;
		ntok = jehanne_tokenize(s, tok, nelem(tok));
		if(!jehanne_strcmp(tok[0], "trace")){	/* 'trace' ktextaddr 'on'|'off'|'mk'|'del' [name] */
			if(ntok < 3) {
				error("devtrace: usage: 'trace' [ktextaddr|name] 'on'|'off'|'mk'|'del' [name]");
			}
			for(pp = &traces; *pp != nil; pp = &(*pp)->next){
				if(!jehanne_strcmp(tok[1], (*pp)->name))
					break;
}
			p = *pp;
			if((ntok > 3) && (!jehanne_strcmp(tok[3], "new"))){
				uintptr_t addr;
				void *start, *end, *func;
				if (ntok != 5) {
					error("devtrace: usage: trace <ktextstart> <ktextend> new <name>");
				}
				addr = (uintptr_t)jehanne_strtoul(tok[1], &ep, 16);
				if (addr < KTZERO)
					addr |= KTZERO;
				func = start = (void *)addr;
				if(*ep) {
					error("devtrace: start address not in recognized format");
				}
				addr = (uintptr_t)jehanne_strtoul(tok[2], &ep, 16);
				if (addr < KTZERO)
					addr |= KTZERO;
				end = (void *)addr;
				if(*ep) {
					error("devtrace: end address not in recognized format");
				}

				if (start > (void *)end || start > (void *)etext || end > (void *)etext)
					error("devtrace: invalid address range");

				/* What do we do here? start and end are weird *
				if((addr < (uintptr_t)start) || (addr > (uintptr_t)end)
					error("devtrace: address out of bounds");
				 */
				if(p) {
					error("devtrace: trace already exists");
				}
				p = mktrace(func, start, end);
				for (foo = traces; foo != nil; foo = foo->next) {
					if (!jehanne_strcmp(tok[4], foo->name))
						error("devtrace: trace with that name already exists");
				}

				if (!overlapping(p)) {
					p->next = traces;
					if(ntok < 5)
						jehanne_snprint(p->name, sizeof p->name, "%p", func);
					else
						jehanne_strncpy(p->name, tok[4], sizeof p->name);
					traces = p;
				} else {
					error("devtrace: given range overlaps with existing trace");
				}
			} else if(!jehanne_strcmp(tok[2], "remove")){
				if (ntok != 3)
					error("devtrace: usage: trace <name> remove");
				if (p == nil) {
					error("devtrace: trace not found");
				}
				removetrace(p);
			} else if(!jehanne_strcmp(tok[2], "on")){
				if (ntok != 3)
					error("devtrace: usage: trace <name> on");

				if(p == nil) {
					error("devtrace: trace not found");
				}
				if (! traced(p->func, 0)){
					traceon(p);
				}
			} else if(!jehanne_strcmp(tok[2], "off")){
				if (ntok != 3)
					error("devtrace: usage: trace <name> off");
				if(p == nil) {
					error("devtrace: trace not found");
				}
				if(traced(p->func, 0)){
					traceoff(p);
				}
			}
		} else if(!jehanne_strcmp(tok[0], "query")){
			/* See if addr is being traced */
			Trace* p;
			uintptr_t addr;
			if (ntok != 2) {
				error("devtrace: usage: query <addr>");
			}
			addr = (uintptr_t)jehanne_strtoul(tok[1], &ep, 16);
			if (addr < KTZERO)
				addr |= KTZERO;
			p = traced((void *)addr, 0);
			if (p) {
				jehanne_print("Probing is enabled\n");
			} else {
				jehanne_print("Probing is disabled\n");
			}
		} else if(!jehanne_strcmp(tok[0], "size")){
			int l, size;
			struct Tracelog *newtracelog;

			if (ntok != 2)
				error("devtrace: usage: size <exponent>");

			l = jehanne_strtoul(tok[1], &ep, 0);
			if(*ep) {
				error("devtrace: size not in recognized format");
			}
			size = 1 << l;
			/* sort of foolish. Alloc new trace first, then free old. */
			/* and too bad if there are unread traces */
			newtracelog = jehanne_mallocz(sizeof(*newtracelog)*size, 1);
			/* does malloc throw waserror? I don't know */
			if (newtracelog){
				jehanne_free(tracelog);
				tracelog = newtracelog;
				logsize = size;
				logmask = size - 1;
				pr = pw = 0;
			} else  {
				error("devtrace:  can't allocate that much");
			}
		} else if (!jehanne_strcmp(tok[0], "testtracein")) {
			/* Manually jump to a certain bit of traced code */
			uintptr_t pc, a1, a2, a3, a4;
			int x;

			if (ntok != 6)
				error("devtrace: usage: testtracein <pc> <arg1> <arg2> <arg3> <arg4>");

			pc = (uintptr_t)jehanne_strtoul(tok[1], &ep, 16);
			if (pc < KTZERO)
				pc |= KTZERO;
			a1 = (uintptr_t)jehanne_strtoul(tok[2], &ep, 16);
			a2 = (uintptr_t)jehanne_strtoul(tok[3], &ep, 16);
			a3 = (uintptr_t)jehanne_strtoul(tok[4], &ep, 16);
			a4 = (uintptr_t)jehanne_strtoul(tok[5], &ep, 16);

			if (traced((void *)pc, 0)) {
				x = splhi();
				watching = 1;
				tracein((void *)pc, a1, a2, a3, a4);
				watching = 0;
				splx(x);
			}
		} else if (!jehanne_strcmp(tok[0], "watch")) {
			/* Watch a certain PID */
			int pid;

			if (ntok != 2) {
				error("devtrace: usage: watch [0|<PID>]");
			}

			pid = jehanne_atoi(tok[1]);
			if (pid == 0) {
				pidwatch = jehanne_mallocz(sizeof(int)*PIDWATCHSIZE, 1);
				numpids = 0;
			} else if (pid < 0) {
				error("PID must be greater than zero.");
			} else if (numpids < PIDWATCHSIZE) {
				pidwatch[numpids] = pid;
				ainc(&numpids);
			} else {
				error("pidwatch array full!");
			}
		} else if (!jehanne_strcmp(tok[0], "start")) {
			if (ntok != 1)
				error("devtrace: usage: start");
			saveactive = 1;
		} else if (!jehanne_strcmp(tok[0], "stop")) {
			if (ntok != 1)
				error("devtrace: usage: stop");
			saveactive = 0;
			all = 0;
		} else if (!jehanne_strcmp(tok[0], "all")) {
			if (ntok != 1)
				error("devtrace: usage: all");
			saveactive = 1;
			all = 1;
		} else {
			error("devtrace:  usage: 'trace' [ktextaddr|name] 'on'|'off'|'mk'|'del' [name] or:  'size' buffersize (power of 2)");
		}
		jehanne_free(s);
		break;
	}
	poperror();
	qunlock(&traceslock);
	traceactive = saveactive;
	return n;
}

Dev tracedevtab = {
	'T',
	"trace",
	devreset,
	devinit,
	devshutdown,
	traceattach,
	tracewalk,
	tracestat,
	traceopen,
	devcreate,
	traceclose,
	traceread,
	devbread,
	tracewrite,
	devbwrite,
	devremove,
	devwstat,
};
