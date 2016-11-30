/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
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
typedef struct Alarms	Alarms;
typedef struct Block	Block;
typedef struct Bpool Bpool;
typedef struct Chan	Chan;
typedef struct Cmdbuf	Cmdbuf;
typedef struct Cmdtab	Cmdtab;
typedef struct Dev	Dev;
typedef struct DevConf	DevConf;
typedef struct Dirtab	Dirtab;
typedef struct Egrp	Egrp;
typedef struct Evalue	Evalue;
typedef struct Fgrp	Fgrp;
typedef struct Ldseg	Ldseg;
typedef struct LockEntry	LockEntry;
typedef struct Log	Log;
typedef struct Logflag	Logflag;
typedef struct Mntcache Mntcache;
typedef struct Mount	Mount;
typedef struct Mntrpc	Mntrpc;
typedef struct Mntwalk	Mntwalk;
typedef struct Mnt	Mnt;
typedef struct Mhead	Mhead;
typedef struct Next	Next;
typedef struct Note	Note;
typedef struct Path	Path;
typedef struct Perf	Perf;
typedef struct PhysUart	PhysUart;
typedef struct Pgrp	Pgrp;
typedef struct Proc	Proc;
typedef struct Procalloc	Procalloc;
typedef struct Profbuf	Profbuf;
typedef struct Pte	Pte;
typedef struct QLock	QLock;
typedef struct Queue	Queue;
typedef struct Ref	Ref;
typedef struct Rendez	Rendez;
typedef struct Rgrp	Rgrp;
typedef struct RMap	RMap;
typedef struct RMapel RMapel;
typedef struct RWlock	RWlock;
typedef struct Schedq	Schedq;
typedef struct Sema	Sema;
typedef struct Timer	Timer;
typedef struct Timers	Timers;
typedef struct Uart	Uart;
typedef struct Waitq	Waitq;
typedef struct Walkqid	Walkqid;
typedef struct Watchdog	Watchdog;
typedef int    Devgen(Chan*, char*, Dirtab*, int, int, Dir*);

#pragma incomplete Bpool
#pragma incomplete DevConf
#pragma incomplete Mntcache
#pragma incomplete Mntrpc
#pragma incomplete Queue
#pragma incomplete Timers

#include <fcall.h>

struct Ref
{
	int	ref;
};

struct LockEntry
{
	LockEntry*	next;
	Lock*		used;
	int		locked;
	int		isilock;
	Mpl		sr;

	/* for debugging */
	uintptr_t	pc;
	Proc*		p;
	Mach*		m;
};

struct Rendez
{
	Lock	l;
	Proc*	p;
};

struct QLock
{
	Lock		use;		/* to access Qlock structure */
	Proc*		head;		/* next process waiting for object */
	Proc*		tail;		/* last process waiting for object */
	uintptr_t	qpc;		/* pc of the holder */
	int		locked;		/* flag */
};

struct RWlock
{
	Lock		use;
	Proc*		head;		/* list of waiting processes */
	Proc*		tail;
	uintptr_t	wpc;		/* pc of writer */
	Proc*		wproc;		/* writing proc */
	int		readers;	/* number of readers */
	int		writer;		/* number of writers */
};

struct RMapel
{
	uintmem	size;
	uintmem	addr;
	RMapel*	next;
};

struct RMap
{
	char*	name;

	RMapel*	(*alloc)(void);
	RMapel*	map;
	RMapel*	free;
	Lock	l;
};

struct Alarms
{
	QLock	ql;
	Proc	*head;
};

/*
 * Access types in namec & channel flags
 */
enum
{
	Aaccess,			/* as in stat, wstat */
	Abind,				/* for left-hand-side of bind */
	Atodir,				/* as in chdir */
	Aopen,				/* for i/o */
	Amount,				/* to be mounted or mounted upon */
	Acreate,			/* is to be created */
	Aremove,			/* will be removed by caller */

	COPEN	= 0x0001,		/* for i/o */
	CMSG	= 0x0002,		/* the message channel for a mount */
/*rsc	CCREATE	= 0x0004,*/		/* permits creation if c->mnt */
	CCEXEC	= 0x0008,		/* close on exec */
	CFREE	= 0x0010,		/* not in use */
	CRCLOSE	= 0x0020,		/* remove on close */
	CCACHE	= 0x0080,		/* client cache */
};

/* flag values */
enum
{
	BINTR	=	(1<<0),

	Bipck	=	(1<<2),		/* ip checksum */
	Budpck	=	(1<<3),		/* udp checksum */
	Btcpck	=	(1<<4),		/* tcp checksum */
	Bpktck	=	(1<<5),		/* packet checksum */
};

struct Block
{
	Block*		next;
	Block*		list;
	uint8_t*	rp;			/* first unconsumed byte */
	uint8_t*	wp;			/* first empty byte */
	uint8_t*	lim;			/* 1 past the end of the buffer */
	uint8_t*	base;			/* start of the buffer */
	void	(*free)(Block*);
	uint8_t	auxspc[64];
	uint16_t	flag;
	uint16_t	checksum;		/* IP checksum of complete packet (minus media header) */
	uint16_t	vlan;
};
#define BLEN(s)	((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base)

struct Chan
{
	Lock		l;
	Ref		r;
	Chan*		next;			/* allocation */
	Chan*		link;
	int64_t		offset;			/* in fd */
	int64_t		devoffset;		/* in underlying device; see read */
	Dev*		dev;
	uint32_t	devno;
	uint16_t	mode;			/* read/write */
	uint16_t	flag;
	Qid		qid;
	int		fid;			/* for devmnt */
	uint32_t	iounit;			/* chunk size for i/o; 0==default */
	Mhead*		umh;			/* mount point that derived Chan; used in unionread */
	Chan*		umc;			/* channel in union; held for union read */
	QLock		umqlock;		/* serialize unionreads */
	int		uri;			/* union read index */
	int		dri;			/* devdirread index */
	uint8_t*	dirrock;		/* directory entry rock for translations */
	int		nrock;
	int		mrock;
	QLock		rockqlock;
	int		ismtpt;
	Mntcache*	mc;			/* Mount cache pointer */
	Mnt*		mux;			/* Mnt for clients using me for messages */
	union {
		void*		aux;
		Qid		pgrpid;		/* for #p/notepg */
		uint32_t	mid;		/* for ns in devproc */
	};
	Chan*		mchan;			/* channel to mounted server */
	Qid		mqid;			/* qid of root of mount point */
	Path*		path;
};

struct Path
{
	Ref	r;
	char*	s;
	Chan**	mtpt;			/* mtpt history */
	int	len;			/* strlen(s) */
	int	alen;			/* allocated length of s */
	int	mlen;			/* number of path elements */
	int	malen;			/* allocated length of mtpt */
};

struct Dev
{
	int	dc;
	char*	name;

	void	(*reset)(void);
	void	(*init)(void);
	void	(*shutdown)(void);
	Chan*	(*attach)(Chan *c, Chan *ac, char *spec, int flags);
	Walkqid*(*walk)(Chan*, Chan*, char**, int);
	long	(*stat)(Chan*, uint8_t*, long);
	Chan*	(*open)(Chan*, int);
	Chan*	(*create)(Chan*, char*, int, int);
	void	(*close)(Chan*);
	long	(*read)(Chan*, void*, long, int64_t);
	Block*	(*bread)(Chan*, long, int64_t);
	long	(*write)(Chan*, void*, long, int64_t);
	long	(*bwrite)(Chan*, Block*, int64_t);
	void	(*remove)(Chan*);
	long	(*wstat)(Chan*, uint8_t*, long);
	void	(*power)(int);	/* power mgt: power(1) => on, power (0) => off */
	int	(*config)(int, char*, DevConf*);	/* returns 0 on error */
};

struct Dirtab
{
	char	name[KNAMELEN];
	Qid	qid;
	int64_t	length;
	long	perm;
};

struct Walkqid
{
	Chan	*clone;
	int	nqid;
	Qid	qid[1];
};

enum
{
	NSMAX	=	1000,
	NSLOG	=	7,
	NSCACHE	=	(1<<NSLOG),
};

struct Mntwalk				/* state for /proc/#/ns */
{
	int	cddone;
	Mhead*	mh;
	Mount*	cm;
};

struct Mount
{
	int	mountid;
	Mount*	next;
	Mhead*	head;
	Mount*	copy;
	Mount*	order;
	Chan*	to;			/* channel replacing channel */
	int	mflag;
	char	*spec;
};

struct Mhead
{
	Ref	r;
	RWlock	lock;
	Chan*	from;			/* channel mounted upon */
	Mount*	mount;			/* what's mounted upon it */
	Mhead*	hash;			/* Hash chain */
};

struct Mnt
{
	Lock		l;
	/* references are counted using c->ref; channels on this mount point incref(c->mchan) == Mnt.c */
	Chan*		c;		/* Channel to file service */
	Proc*		rip;		/* Reader in progress */
	Mntrpc*		queue;		/* Queue of pending requests on this channel */
	uint32_t	id;		/* Multiplexer id for channel check */
	Mnt*		list;		/* Free list */
	int		flags;		/* cache */
	int		msize;		/* data + IOHDRSZ */
	char*		version;	/* 9P version */
	Queue*		q;		/* input queue */
};

enum
{
	NUser,				/* note provided externally */
	NExit,				/* deliver note quietly */
	NDebug,				/* print debug message */
};

struct Note
{
	char	msg[ERRMAX];
	int	flag;			/* whether system posted it */
};

enum
{
	PG_NOFLUSH	= 0,
	PG_TXTFLUSH	= 1,		/* flush dcache and invalidate icache */
	PG_DATFLUSH	= 2,		/* flush both i & d caches (UNUSED) */
};

#define	pagesize(p)	(1<<(p)->lg2size)

struct Profbuf
{
	Ref		r;
	uint64_t*	ticks;	/* Tick profile area */
};

struct Sema
{
	Rendez	rend;
	int*	addr;
	int	waiting;
	Sema*	next;
	Sema*	prev;
};

#include "../port/umem/umem.h"

/* demand loading params of a segment */
struct Ldseg {
	int64_t		memsz;
	int64_t		filesz;
	int64_t		pg0fileoff;
	uintptr_t	pg0vaddr;
	uint32_t	pg0off;
	uint32_t	pgsz;
	SegmentType 	type		: 3;
	SegPermission	permissions	: 3;
	SegFlag		flags		: 2;
};

enum
{
	RENDHASH =	31,	/* Hash to lookup rendezvous tags */
	MNTLOG	=	5,
	MNTHASH =	1<<MNTLOG,	/* Hash to walk mount table */
	NFD =		100,		/* per process file descriptors */
	PGHLOG  =	9,
	PGHSIZE	=	1<<PGHLOG,	/* Page hash for image lookup */
};
#define REND(p,s)	((p)->rendhash[(uintptr_t)(s)%RENDHASH])
#define MOUNTH(p,qid)	((p)->mnthash[(qid).path&((1<<MNTLOG)-1)])

struct Pgrp
{
	Ref		r;
	int		noattach;
	uint32_t	pgrpid;
	QLock		debug;			/* single access via devproc.c */
	RWlock		ns;			/* Namespace n read/one write lock */
	Mhead*		mnthash[MNTHASH];
};

struct Rgrp
{
	Lock	l;
	Ref	r;
	Proc*	rendhash[RENDHASH];	/* Rendezvous tag hash */
};

struct Egrp
{
	Ref		r;
	RWlock		rwl;
	Evalue**	ent;
	int		nent;
	int		ment;
	uint32_t	path;	/* qid.path of next Evalue to be allocated */
	uint32_t	vers;	/* of Egrp */
};

struct Evalue
{
	char	*name;
	char	*value;
	int	len;
	Evalue	*link;
	Qid	qid;
};

struct Fgrp
{
	Lock	l;
	Ref	r;
	Chan	**fd;
	int	nfd;			/* number allocated */
	int	maxfd;			/* highest fd in use */
	int	exceed;			/* debugging */
};

enum
{
	DELTAFD	= 20		/* incremental increase in Fgrp.fd's */
};

struct Waitq
{
	Waitmsg	w;
	Waitq*	next;
};

/*
 * fasttick timer interrupts
 */
typedef enum TimerMode {
	Trelative,	/* timer programmed in ns from now */
	Tperiodic,	/* periodic timer, period in ns */
} TimerMode;

struct Timer
{
	/* Internal */
	Lock		l;
	Timers*		tt;	/* Timers queue this timer runs on */
	int64_t		twhen;	/* ns represented in fastticks */
	Timer*		tnext;

	/* Public interface */
	int64_t		tns;	/* meaning defined by mode */
	void		(*tf)(Ureg*, Timer*);
	void*		ta;
	TimerMode	tmode;
};

enum
{
	RFNAMEG		= (1<<0),
	RFENVG		= (1<<1),
	RFFDG		= (1<<2),
	RFNOTEG		= (1<<3),
	RFPROC		= (1<<4),
	RFMEM		= (1<<5),
	RFNOWAIT	= (1<<6),
	RFCNAMEG	= (1<<10),
	RFCENVG		= (1<<11),
	RFCFDG		= (1<<12),
	RFREND		= (1<<13),
	RFNOMNT		= (1<<14),
};

/*
 *  process memory segments - NSEG always last !
 */
enum
{
	SSEG, TSEG, DSEG, BSEG, ESEG, LSEG, SEG1, SEG2, SEG3, SEG4, NSEG
};

enum
{
	Dead = 0,		/* Process states */
	Moribund,
	Broken,
	Ready,
	Scheding,
	Running,
	Queueing,
	QueueingR,
	QueueingW,
	Wakeme,
	Stopped,
	Rendezvous,
	Waitrelease,

	Proc_stopme = 1, 	/* devproc requests */
	Proc_exitme,
	Proc_traceme,
	Proc_exitbig,
	Proc_tracesyscall,

	TUser = 0, 		/* Proc.time */
	TSys,
	TReal,
	TCUser,
	TCSys,
	TCReal,

	NERR = 64,
	NNOTE = 5,

	Npriq		= 20,		/* number of scheduler priority levels */
	PriNormal	= 10,		/* base priority for normal processes */
	PriKproc	= 13,		/* base priority for kernel processes */
	PriRoot		= 13,		/* base priority for root processes */

};

struct Schedq
{
	Lock	l;
	Proc*	head;
	Proc*	tail;
	int	n;
};

typedef union ScRet ScRet;
union ScRet {
	int		i;
	long		l;
	uintptr_t	p;
	usize		u;
	void*		v;
	int64_t		vl;
};

struct Proc
{
	Label		sched;		/* known to l.s */
	char*		kstack;		/* known to l.s */
	void*		dbgreg;		/* known to l.s User registers for devproc */
	Mach*		mach;		/* machine running this proc */
	char*		text;
	char*		user;
	char**		args;
	int		nargs;		/* number of bytes of args */
	Proc*		rnext;		/* next process in run queue */
	Proc*		qnext;		/* next process on queue for a QLock */
	QLock*		qlock;		/* addr of qlock being queued for DEBUG */
	int		state;
	char*		psstate;	/* What /proc/#/status reports */
	ProcSegment*	seg[NSEG];
	RWlock		seglock;	/* wlocked whenever seg[] changes */
	int		pid;
	int		index;		/* index (slot) in proc array */
	int		ref;		/* indirect reference */
	int		noteid;		/* Equivalent of note group */
	Proc*		pidhash;	/* next proc in pid hash */

	Lock		exl;		/* Lock count and waitq */
	Waitq*		waitq;		/* Exited processes wait children */
	int		nchild;		/* Number of living children */
	int		nwait;		/* Number of uncollected wait records */
	QLock		qwaitr;
	Rendez		waitr;		/* Place to hang out in wait */
	Proc*		parent;

	Pgrp*		pgrp;		/* Process group for namespace */
	Egrp*		egrp;		/* Environment group */
	Fgrp*		fgrp;		/* File descriptor group */
	Rgrp*		rgrp;		/* Rendez group */

	Fgrp*		closingfgrp;	/* used during teardown */

	int		parentpid;
	uint64_t	time[6];	/* User, Sys, Real; child U, S, R */

	uint64_t	kentry;		/* Kernel entry time stamp (for profiling) */
	/*
	 * pcycles: cycles spent in this process (updated on procsave/restore)
	 * when this is the current proc and we're in the kernel
	 * (procrestores outnumber procsaves by one)
	 * the number of cycles spent in the proc is pcycles + cycles()
	 * when this is not the current process or we're in user mode
	 * (procrestores and procsaves balance), it is pcycles.
	 */
	int64_t		pcycles;
	Profbuf*	prof;

	int		insyscall;
	int32_t		blockingfd;	/* fd currenly read/written */

	QLock		debug;		/* to access debugging elements of User */
	Proc*		pdbg;		/* the debugging process */
	uint32_t	procmode;	/* proc device file mode */
	uint32_t	privatemem;	/* proc does not let anyone read mem */
	int		hang;		/* hang at next exec for debug */
	int		procctl;	/* Control for /proc debugging */
	uintptr_t	pc;		/* DEBUG only */

	Lock		rlock;		/* sync sleep/wakeup with postnote */
	Rendez*		r;		/* rendezvous point slept on */
	Rendez		sleep;		/* place for syssleep/debug */
	int		notepending;	/* note issued but not acted on */
	int		notedeferred;	/* note not to be acted on immediately */
	int		kp;		/* true if a kernel process */
	Proc*		palarm;		/* Next alarm time */
	uint32_t	alarm;		/* Time of call */
	uint32_t	wakeups;	/* Number of pending wakeups */
	uint64_t	pendingWakeup;	/* checked on blocking syscall enter */
	uint64_t	lastWakeup;	/* set on blocking syscall exit */
	int		newtlb;		/* Pager has changed my pte's, I must flush */
	int		noswap;		/* process is not swappable */

	uintptr_t	rendtag;	/* Tag for rendezvous */
	uintptr_t	rendval;	/* Value for rendezvous */
	Proc*		rendhash;	/* Hash list for tag values */

	Timer;			/* For tsleep and real-time */
	Rendez*		trend;
	int		(*tfn)(void*);
	void		(*kpfun)(void*);
	void*		kparg;

	int		scallnr;	/* system call number */
	uint8_t		arg[MAXSYSARG*sizeof(void*)];	/* system call arguments */
	int		nerrlab;
	Label		errlab[NERR];
	long		syscallerr;	/* (negative) error code to return from syscalls */
	char*		syserrstr;	/* last error from a system call, errbuf0 or 1 */
	char*		errstr;		/* reason we're unwinding the error stack, errbuf1 or 0 */
	char		errbuf0[ERRMAX];
	char		errbuf1[ERRMAX];
	char		genbuf[128];	/* buffer used e.g. for last name element from namec */
	Chan*		slash;
	Chan*		dot;

	Note		note[NNOTE];
	short		nnote;
	short		notified;	/* sysnoted is due */
	Note		lastnote;
	void		(*notify)(void*, char*);

	Lock*		lockwait;
	Lock*		lastlock;	/* debugging */
	Lock*		lastilock;	/* debugging */

	Mach*		wired;
	Mach*		mp;		/* machine this process last ran on */
	int		nlocks;		/* number of locks held by proc */
	int		lockdepth;
	uint32_t	delaysched;
	uint32_t	priority;	/* priority level */
	uint32_t	basepri;	/* base priority level */
	int		fixedpri;	/* priority level does not change */
	uint64_t	cpu;		/* cpu average */
	uint64_t	lastupdate;
	uint64_t	readytime;	/* time process came ready */
	uint64_t	movetime;	/* last time process switched processors */
	int		preempted;	/* true if this process hasn't finished the interrupt
					 *  that last preempted it
					 */
	int		trace;		/* process being traced? */

	uintptr_t	qpc;		/* pc calling last blocking qlock */

	int		setargs;

	void*		ureg;		/* User registers for notes */

	Queue*		syscallq;

	/*
	 *  machine specific fpu, mmu and notify
	 */
	PFPU	FPU;
	PMMU;
	PNOTIFY;
};

struct Procalloc
{
	Lock	l;
	Proc*	ht[128];
	Proc*	arena;
	Proc*	free;
	int	nproc;
};

enum
{
	PRINTSIZE =	256,
	NUMSIZE	=	12,		/* size of formatted number */
	MB =		(1024*1024),
	/* READSTR was 1000, which is way too small for usb's ctl file */
	READSTR =	4000,		/* temporary buffer size for device reads */
};

extern	char*	conffile;
extern	int	cpuserver;
extern	char*	cputype;
extern  char*	eve;
extern	char	hostdomain[];
extern	uint8_t	initcode[];
extern	int	kbdbuttons;
extern  Ref	noteidalloc;
extern	int	nphysseg;
extern	int	nsyscall;
extern	Procalloc	procalloc;
extern	RMap rmapram;
extern	uint32_t	qiomaxatomic;
extern	char*	statename[];
extern	char*	sysname;

enum
{
	LRESPROF	= 3,
};

/*
 *  action log
 */
struct Log {
	Lock	l;
	int	opens;
	char*	buf;
	char	*end;
	char	*rptr;
	int	len;
	int	nlog;
	int	minread;

	int	logmask;	/* mask of things to debug */

	QLock	readq;
	Rendez	readr;
};

struct Logflag {
	char*	name;
	int	mask;
};

enum
{
	NCMDFIELD = 128
};

struct Cmdbuf
{
	char	*buf;
	char	**f;
	int	nf;
};

struct Cmdtab
{
	int	index;	/* used by client to switch on result */
	char	*cmd;	/* command name */
	int	narg;	/* expected #args; 0 ==> variadic */
};

/*
 *  routines to access UART hardware
 */
struct PhysUart
{
	char*	name;
	Uart*	(*pnp)(void);
	void	(*enable)(Uart*, int);
	void	(*disable)(Uart*);
	void	(*kick)(Uart*);
	void	(*dobreak)(Uart*, int);
	int	(*baud)(Uart*, int);
	int	(*bits)(Uart*, int);
	int	(*stop)(Uart*, int);
	int	(*parity)(Uart*, int);
	void	(*modemctl)(Uart*, int);
	void	(*rts)(Uart*, int);
	void	(*dtr)(Uart*, int);
	long	(*status)(Uart*, void*, long, long);
	void	(*fifo)(Uart*, int);
	void	(*power)(Uart*, int);
	int	(*getc)(Uart*);			/* polling version for rdb */
	void	(*putc)(Uart*, int);		/* polling version for iprint */
	void	(*poll)(Uart*);			/* polled interrupt routine */
};

enum {
	Stagesize=	2048
};

/*
 *  software UART
 */
struct Uart
{
	void*	regs;			/* hardware stuff */
	void*	saveregs;		/* place to put registers on power down */
	char*	name;			/* internal name */
	uint32_t	freq;			/* clock frequency */
	int	bits;			/* bits per character */
	int	stop;			/* stop bits */
	int	parity;			/* even, odd or no parity */
	int	baud;			/* baud rate */
	PhysUart*phys;
	int	console;		/* used as a serial console */
	int	special;		/* internal kernel device */
	Uart*	next;			/* list of allocated uarts */

	QLock	ql;
	int	type;			/* ?? */
	int	dev;
	int	opens;

	int	enabled;
	Uart	*elist;			/* next enabled interface */

	int	perr;			/* parity errors */
	int	ferr;			/* framing errors */
	int	oerr;			/* rcvr overruns */
	int	berr;			/* no input buffers */
	int	serr;			/* input queue overflow */

	/* buffers */
	int	(*putc)(Queue*, int);
	Queue	*iq;
	Queue	*oq;

	Lock	rlock;
	uint8_t	istage[Stagesize];
	uint8_t	*iw;
	uint8_t	*ir;
	uint8_t	*ie;

	Lock	tlock;			/* transmit */
	uint8_t	ostage[Stagesize];
	uint8_t	*op;
	uint8_t	*oe;
	int	drain;

	int	modem;			/* hardware flow control on */
	int	xonoff;			/* software flow control on */
	int	blocked;
	int	cts, dsr, dcd;		/* keep track of modem status */
	int	ctsbackoff;
	int	hup_dsr, hup_dcd;	/* send hangup upstream? */
	int	dohup;

	Rendez	r;
};

extern	Uart*	consuart;

/*
 *  performance timers, all units in perfticks
 */
struct Perf
{
	uint64_t	intrts;		/* time of last interrupt */
	uint64_t	inintr;		/* time since last clock tick in interrupt handlers */
	uint64_t	avg_inintr;	/* avg time per clock tick in interrupt handlers */
	uint64_t	inidle;		/* time since last clock tick in idle loop */
	uint64_t	avg_inidle;	/* avg time per clock tick in idle loop */
	uint64_t	last;		/* value of perfticks() at last clock tick */
	uint64_t	period;		/* perfticks() per clock tick */
};

struct Watchdog
{
	void	(*enable)(void);	/* watchdog enable */
	void	(*disable)(void);	/* watchdog disable */
	void	(*restart)(void);	/* watchdog restart */
	void	(*stat)(char*, char*);	/* watchdog statistics */
};

/* queue state bits,  Qmsg, Qcoalesce, and Qkick can be set in qopen */
enum
{
	/* Queue.state */
	Qstarve		= (1<<0),	/* consumer starved */
	Qmsg		= (1<<1),	/* message stream */
	Qclosed		= (1<<2),	/* queue has been closed/hungup */
	Qflow		= (1<<3),	/* producer flow controlled */
	Qcoalesce	= (1<<4),	/* coalesce packets on read */
	Qkick		= (1<<5),	/* always call the kick routine after qwrite */
};

#define DEVDOTDOT -1

#pragma	varargck	type	"I"	uint8_t*
#pragma	varargck	type	"V"	uint8_t*
#pragma	varargck	type	"E"	uint8_t*
#pragma	varargck	type	"M"	uint8_t*

#pragma	varargck	type	"m"	Mreg
#pragma	varargck	type	"P"	uintmem
