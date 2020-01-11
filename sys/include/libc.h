/*
 * Copyright (C) 2015-2020 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#pragma	lib	"libjehanne.a"
#pragma	src	"/sys/src/lib/jehanne"

#define JEHANNE_LIBC	/* only for native code */

#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define	offsetof(s, m)	(uintptr_t)(&(((s*)0)->m))
#define	assert(x)	if(x){}else jehanne__assert(#x)

extern void (*_abort)(void);
#define abort() if(_abort){_abort();}else{while(*(int*)0);}

/*
 * mem routines
 */
extern	void*	jehanne_memccpy(void*, const void*, int, uint32_t);
extern	void*	jehanne_memset(void*, int, uint32_t);
extern	int	jehanne_memcmp(const void*, const void*, uint32_t);
extern	void*	jehanne_memcpy(void*, const void*, size_t);
extern	void*	jehanne_memmove(void*, const void*, size_t);
extern	void*	jehanne_memchr(const void*, int, uint32_t);

/*
 * string routines
 */
extern	char*	jehanne_strcat(char*, const char*);
extern	char*	jehanne_strchr(const char*, int);
extern	int	jehanne_strcmp(const char*, const char*);
extern	char*	jehanne_strcpy(char*, const char*);
extern	char*	jehanne_strecpy(char*, char *, const char*);
extern	char*	jehanne_strdup(const char*);
extern	char*	jehanne_strncat(char*, const char*, int32_t);
extern	char*	jehanne_strncpy(char*, const char*, uint32_t);
extern	int	jehanne_strncmp(const char*, const char*, int32_t);
extern	char*	jehanne_strpbrk(const char*, const char*);
extern	char*	jehanne_strrchr(const char*, int);
extern	char*	jehanne_strtok(char*, char*);
extern	int	jehanne_strlen(const char*);
extern	int32_t	jehanne_strspn(const char*, const char*);
extern	int32_t	jehanne_strcspn(const char*, const char*);
extern	char*	jehanne_strstr(const char*, const char*);
extern	int	jehanne_cistrncmp(const char*, const char*, int);
extern	int	jehanne_cistrcmp(const char*, const char*);
extern	char*	jehanne_cistrstr(const char*, const char*);
extern	int	jehanne_tokenize(char*, char**, int);

enum
{
	UTFmax		= 4,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
	Runemax		= 0x10FFFF,	/* 21-bit rune */
	Runemask	= 0x1FFFFF,	/* bits used by runes (see grep) */
};

/*
 * rune routines
 */
extern	int	jehanne_runetochar(char*, const Rune*);
extern	int	jehanne_chartorune(Rune*, const char*);
extern	int	jehanne_runelen(Rune);
extern	int	jehanne_runenlen(const Rune*, int);
extern	int	jehanne_fullrune(const char*, int);
extern	int	jehanne_utflen(const char*);
extern	int	jehanne_utfnlen(const char*, int32_t);
extern	char*	jehanne_utfrune(const char*, Rune);
extern	char*	jehanne_utfrrune(const char*, Rune);
extern	char*	jehanne_utfutf(const char*, const char*);
extern	char*	jehanne_utfecpy(char*, char *, const char*);

extern	Rune*	jehanne_runestrcat(Rune*, const Rune*);
extern	Rune*	jehanne_runestrchr(const Rune*, Rune);
extern	int	jehanne_runestrcmp(const Rune*, const Rune*);
extern	Rune*	jehanne_runestrcpy(Rune*, const Rune*);
extern	Rune*	jehanne_runestrncpy(Rune*, const Rune*, int32_t);
extern	Rune*	jehanne_runestrecpy(Rune*, Rune*, const Rune*);
extern	Rune*	jehanne_runestrdup(const Rune*);
extern	Rune*	jehanne_runestrncat(Rune*, const Rune*, int32_t);
extern	int	jehanne_runestrncmp(const Rune*, const Rune*, int32_t);
extern	Rune*	jehanne_runestrrchr(const Rune*, Rune);
extern	int32_t	jehanne_runestrlen(const Rune*);
extern	Rune*	jehanne_runestrstr(const Rune*, const Rune*);

extern	Rune	jehanne_tolowerrune(Rune);
extern	Rune	jehanne_totitlerune(Rune);
extern	Rune	jehanne_toupperrune(Rune);
extern	Rune	jehanne_tobaserune(Rune);
extern	int	jehanne_isalpharune(Rune);
extern	int	jehanne_isbaserune(Rune);
extern	int	jehanne_isdigitrune(Rune);
extern	int	jehanne_islowerrune(Rune);
extern	int	jehanne_isspacerune(Rune);
extern	int	jehanne_istitlerune(Rune);
extern	int	jehanne_isupperrune(Rune);

/*
 * malloc
 */
extern	void*	jehanne_malloc(size_t);
extern	void*	jehanne_mallocz(uint32_t, int);
extern	void	jehanne_free(void*);
extern	uint32_t	jehanne_msize(void*);
extern	void*	jehanne_mallocalign(uint32_t, uint32_t, int32_t, uint32_t);
extern	void*	jehanne_calloc(uint32_t, size_t);
extern	void*	jehanne_realloc(void*, size_t);
void	jehanne_setmalloctag(void*, uintptr_t);
void	jehanne_setrealloctag(void*, uintptr_t);
uintptr_t	jehanne_getmalloctag(void*);
uintptr_t	jehanne_getrealloctag(void*);
void*	jehanne_malloctopoolblock(void*);

/*
 * print routines
 */
typedef struct Fmt	Fmt;
struct Fmt{
	uint8_t	runes;			/* output buffer is runes or chars? */
	void	*start;			/* of buffer */
	void	*to;			/* current place in the buffer */
	void	*stop;			/* end of the buffer; overwritten if flush fails */
	int	(*flush)(Fmt *);	/* called when to == stop */
	void	*farg;			/* to make flush a closure */
	int	nfmt;			/* num chars formatted so far */
	va_list	args;			/* args passed to dofmt */
	int	r;			/* % format Rune */
	int	width;
	int	prec;
	uint32_t	flags;
};

enum{
	FmtWidth	= 1,
	FmtLeft		= FmtWidth << 1,
	FmtPrec		= FmtLeft << 1,
	FmtSharp	= FmtPrec << 1,
	FmtSpace	= FmtSharp << 1,
	FmtSign		= FmtSpace << 1,
	FmtZero		= FmtSign << 1,
	FmtUnsigned	= FmtZero << 1,
	FmtShort	= FmtUnsigned << 1,
	FmtLong		= FmtShort << 1,
	FmtVLong	= FmtLong << 1,
	FmtComma	= FmtVLong << 1,
	FmtByte		= FmtComma << 1,

	FmtFlag		= FmtByte << 1
};

extern	int	jehanne_print(const char*, ...);
extern	char*	jehanne_seprint(char*, char*, const char*, ...);
extern	char*	jehanne_vseprint(char*, char*, const char*, va_list);
extern	int	jehanne_snprint(char*, int, const char*, ...);
extern	int	jehanne_vsnprint(char*, int, const char*, va_list);
extern	char*	jehanne_smprint(const char*, ...);
extern	char*	jehanne_vsmprint(const char*, va_list);
extern	int	jehanne_sprint(char*, const char*, ...);
extern	int	jehanne_fprint(int, const char*, ...);
extern	int	jehanne_vfprint(int, const char*, va_list);

extern	int	jehanne_runesprint(Rune*, const char*, ...);
extern	int	jehanne_runesnprint(Rune*, int, const char*, ...);
extern	int	jehanne_runevsnprint(Rune*, int, const char*, va_list);
extern	Rune*	jehanne_runeseprint(Rune*, Rune*, const char*, ...);
extern	Rune*	jehanne_runevseprint(Rune*, Rune*, const char*, va_list);
extern	Rune*	jehanne_runesmprint(const char*, ...);
extern	Rune*	jehanne_runevsmprint(const char*, va_list);

extern	int	jehanne_fmtfdinit(Fmt*, int, char*, int);
extern	int	jehanne_fmtfdflush(Fmt*);
extern	int	jehanne_fmtstrinit(Fmt*);
extern	char*	jehanne_fmtstrflush(Fmt*);
extern	int	jehanne_runefmtstrinit(Fmt*);
extern	Rune*	jehanne_runefmtstrflush(Fmt*);

extern	int	jehanne_fmtinstall(int, int (*)(Fmt*));
extern	int	jehanne_dofmt(Fmt*, const char*);
extern	int	jehanne_dorfmt(Fmt*, const Rune*);
extern	int	jehanne_fmtprint(Fmt*, const char*, ...);
extern	int	jehanne_fmtvprint(Fmt*, const char*, va_list);
extern	int	jehanne_fmtrune(Fmt*, int);
extern	int	jehanne_fmtstrcpy(Fmt*, const char*);
extern	int	jehanne_fmtrunestrcpy(Fmt*, const Rune*);
/*
 * error string for %r
 * supplied on per os basis, not part of fmt library
 */
extern	int	jehanne_errfmt(Fmt *f);

/*
 * quoted strings
 */
extern	char	*jehanne_unquotestrdup(const char*);
extern	Rune	*jehanne_unquoterunestrdup(const Rune*);
extern	char	*jehanne_quotestrdup(const char*);
extern	Rune	*jehanne_quoterunestrdup(const Rune*);
extern	int	jehanne_quotestrfmt(Fmt*);
extern	int	jehanne_quoterunestrfmt(Fmt*);
extern	void	jehanne_quotefmtinstall(void);
extern	int	(*doquote)(int);
extern	int	jehanne_needsrcquote(int);

/*
 * random number
 */
extern	void	jehanne_srand(int32_t);
extern	int	jehanne_rand(void);
extern	int	jehanne_nrand(int);
extern	int32_t	jehanne_lrand(void);
extern	int32_t	jehanne_lnrand(int32_t);
extern	double	jehanne_frand(void);
extern	uint32_t	jehanne_truerand(void);			/* uses /dev/random */
extern	uint32_t	jehanne_ntruerand(uint32_t);		/* uses /dev/random */

/*
 * math
 */
extern	uint32_t	jehanne_getfcr(void);
extern	void	jehanne_setfsr(uint32_t);
extern	uint32_t	getfsr(void);
extern	void	jehanne_setfcr(uint32_t);
extern	double	jehanne_NaN(void);
extern	double	jehanne_Inf(int);
extern	int	jehanne_isNaN(double);
extern	int	jehanne_isInf(double, int);
extern	uint32_t	jehanne_umuldiv(uint32_t, uint32_t, uint32_t);
extern	int32_t	jehanne_muldiv(int32_t, int32_t, int32_t);

extern	double	jehanne_pow(double, double);
extern	double	jehanne_atan2(double, double);
extern	double	jehanne_fabs(double);
extern	double	jehanne_atan(double);
extern	double	jehanne_log(double);
extern	double	jehanne_log10(double);
extern	double	jehanne_exp(double);
extern	double	jehanne_floor(double);
extern	double	jehanne_ceil(double);
extern	double	jehanne_hypot(double, double);
extern	double	jehanne_sin(double);
extern	double	jehanne_cos(double);
extern	double	jehanne_tan(double);
extern	double	jehanne_asin(double);
extern	double	jehanne_acos(double);
extern	double	jehanne_sinh(double);
extern	double	jehanne_cosh(double);
extern	double	jehanne_tanh(double);
extern	double	jehanne_sqrt(double);
extern	double	jehanne_fmod(double, double);

#define	HUGE	3.4028234e38
#define	PIO2	1.570796326794896619231e0
#define	PI	(PIO2+PIO2)

/*
 * Time-of-day
 */

typedef
struct Tm
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
	int	wday;
	int	yday;
	char	zone[4];
	int	tzoff;
} Tm;

extern	Tm*	jehanne_gmtime(int32_t);
extern	Tm*	jehanne_localtime(int32_t);
extern	char*	jehanne_asctime(Tm*);
extern	char*	jehanne_ctime(int32_t);
extern	double	jehanne_cputime(void);
extern	int32_t	jehanne_times(int32_t*);
extern	int32_t	jehanne_tm2sec(Tm*);
//extern	int64_t	jehanne_nsec(void);

extern	void	cycles(uint64_t*);	/* 64-bit value of the cycle counter if there is one, 0 if there isn't */

/*
 * one-of-a-kind
 */
enum
{
	PNPROC		= 1,
	PNGROUP		= 2,
};

extern	void	jehanne__assert(const char*) __attribute__ ((noreturn));
extern	int	jehanne_abs(int);
extern	int	jehanne_atexit(void(*)(void));
extern	void	jehanne_atexitdont(void(*)(void));
extern	int	jehanne_atnotify(int(*)(void*, char*), int);
extern	double	jehanne_atof(const char*);
extern	int	jehanne_atoi(const char*);
extern	int32_t	jehanne_atol(const char*);
extern	int64_t	jehanne_atoll(const char*);
extern	double	jehanne_charstod(int(*)(void*), void*);
extern	int	jehanne_chdir(const char *dirname);
extern	char*	jehanne_cleanname(char*);
extern	int	decrypt(void*, void*, int);
extern	int	encrypt(void*, void*, int);
extern	int	jehanne_dec64(uint8_t*, int, const char*, int);
extern	int	jehanne_enc64(char*, int, const uint8_t*, int);
extern	int	jehanne_dec32(uint8_t*, int, const char*, int);
extern	int	jehanne_enc32(char*, int, const uint8_t*, int);
extern	int	jehanne_dec16(uint8_t*, int, const char*, int);
extern	int	jehanne_enc16(char*, int, const uint8_t*, int);
extern	int	jehanne_encodefmt(Fmt*);
extern	void	jehanne_exits(const char*) __attribute__ ((noreturn));
extern	double	jehanne_frexp(double, int*);
extern	uintptr_t	jehanne_getcallerpc(void);
extern	int	jehanne_getfields(char*, char**, int, int, const char*);
extern	int	jehanne_gettokens(char *, char **, int, const char *);
extern	char*	jehanne_getuser(void);
extern	long	jehanne_getwd(char*, int);
extern	int	jehanne_iounit(int);
extern	int32_t	jehanne_labs(int32_t);
extern	double	jehanne_ldexp(double, int);
extern	void	jehanne_longjmp(jmp_buf, int);
extern	char*	jehanne_mktemp(char*);
extern	double	jehanne_modf(double, double*);
extern	void	jehanne_notejmp(void*, jmp_buf, int);
extern 	unsigned long jehanne_nsec(void);
extern	int	jehanne_dup(int oldfd, int newfd);
extern	int	jehanne_ocreate(const char* path, unsigned int omode, unsigned int perm);
extern	void	jehanne_perror(const char*);
extern	int	jehanne_pipe(int pipes[2]);
extern	int	jehanne_postnote(int, int, const char *);
extern	double	jehanne_pow10(int);
extern	void	jehanne_qsort(void*, long, int,
			int (*)(const void*, const void*));
extern	void*	jehanne_bsearch(const void* key, const void* base, size_t nmemb, size_t size,
			int (*compar)(const void*, const void*));
extern	int	jehanne_setjmp(jmp_buf);
extern	double	jehanne_strtod(const char*, const char**);
extern	int32_t	jehanne_strtol(const char*, char**, int);
extern	uint32_t	jehanne_strtoul(const char*, char**, int);
extern	int64_t	jehanne_strtoll(const char*, char**, int);
extern	uint64_t	jehanne_strtoull(const char*, char**, int);
extern	void	jehanne_sysfatal(const char*, ...) __attribute__ ((noreturn));
extern	void	jehanne_syslog(int, const char*, const char*, ...);
extern	int32_t	jehanne_time(int32_t*);
extern	int	jehanne_tolower(int);
extern	int	jehanne_toupper(int);

/*
 * atomic
 */
int32_t	jehanne_ainc(int32_t*);
int32_t	jehanne_adec(int32_t*);
#define cas(ptr, oldval, newval) __sync_bool_compare_and_swap(ptr, oldval, newval)
#define casv(ptr, oldval, newval) __sync_val_compare_and_swap(ptr, oldval, newval)

/*
 *  synchronization
 */
typedef
struct Lock {
	int32_t	key;
	int32_t	sem;
} Lock;

extern int	jehanne_tsemacquire(int* addr, long ms);

extern int	jehanne__tas(int*);

extern	void	jehanne_lock(Lock*);
extern	int	jehanne_lockt(Lock*, uint32_t);
extern	void	jehanne_unlock(Lock*);
extern	int	jehanne_canlock(Lock*);

typedef struct QLp QLp;
struct QLp
{
	uint8_t	state;
	QLp	*next;
};

typedef
struct QLock
{
	Lock	lock;
	int	locked;
	QLp	*head;
	QLp 	*tail;
} QLock;

extern	void	jehanne_qlock(QLock*);
extern	int	jehanne_qlockt(QLock*, uint32_t);
extern	void	jehanne_qunlock(QLock*);
extern	int	jehanne_canqlock(QLock*);
extern	void	jehanne__qlockinit(void* (*)(void*, void*));	/* called only by the thread library */

typedef
struct RWLock
{
	Lock	lock;
	int	_readers;	/* number of readers */
	int	writer;		/* number of writers */
	QLp	*head;		/* list of waiting processes */
	QLp	*tail;
} RWLock;

extern	void	jehanne_rlock(RWLock*);
extern	int	jehanne_rlockt(RWLock*, uint32_t);
extern	void	jehanne_runlock(RWLock*);
extern	int	jehanne_canrlock(RWLock*);
extern	void	jehanne_wlock(RWLock*);
extern	int	jehanne_wlockt(RWLock*, uint32_t);
extern	void	jehanne_wunlock(RWLock*);
extern	int	jehanne_canwlock(RWLock*);

typedef
struct Rendez
{
	QLock	*l;
	QLp	*head;
	QLp	*tail;
} Rendez;

extern	void	jehanne_rsleep(Rendez*);	/* unlocks r->l, sleeps, locks r->l again */
extern	int	jehanne_rsleept(Rendez*, uint32_t);	/* unlocks r->l, sleeps (up to ms), locks r->l again (if not timedout) */
extern	int	jehanne_rwakeup(Rendez*);
extern	int	jehanne_rwakeupall(Rendez*);
extern	void**	jehanne_privalloc(void);

/*
 *  network dialing
 */
#define NETPATHLEN 40
extern	int	jehanne_accept(int, const char*);
extern	int	jehanne_announce(const char*, char*);
extern	int	jehanne_dial(const char*, const char*, char*, int*);
extern	void	jehanne_setnetmtpt(char*, int, const char*);
extern	int	jehanne_hangup(int);
extern	int	jehanne_listen(const char*, char*);
extern	char*	jehanne_netmkaddr(const char*, const char*, const char*);
extern	int	jehanne_reject(int, const char*, const char*);

/*
 *  encryption
 */
extern	int	jehanne_pushssl(int, const char*, const char*, const char*, int*);
//extern	int	jehanne_pushtls(int, const char*, const char*, int, const char*,
//				 char*);

/*
 *  network services
 */
typedef struct NetConnInfo NetConnInfo;
struct NetConnInfo
{
	char	*dir;		/* connection directory */
	char	*root;		/* network root */
	char	*spec;		/* binding spec */
	char	*lsys;		/* local system */
	char	*lserv;		/* local service */
	char	*rsys;		/* remote system */
	char	*rserv;		/* remote service */
	char	*laddr;		/* local address */
	char	*raddr;		/* remote address */
};
extern	NetConnInfo*	jehanne_getnetconninfo(const char*, int);
extern	void		jehanne_freenetconninfo(NetConnInfo*);

/*
 * system calls
 *
 */
#define	STATMAX	65535U	/* max length of machine-independent stat structure */
#define	DIRMAX	(sizeof(Dir)+STATMAX)	/* max length of Dir structure */
#define	ERRMAX	128	/* max length of error string */

#define	MORDER	0x0003	/* mask for bits defining order of mounting */
#define	MREPL	0x0000	/* mount replaces object */
#define	MBEFORE	0x0001	/* mount goes before others in union directory */
#define	MAFTER	0x0002	/* mount goes after others in union directory */
#define	MCREATE	0x0004	/* permit creation in mounted directory */
#define	MCACHE	0x0010	/* cache some data */
#define	MMASK	0x0017	/* all bits on */

/* Open modes: Kernel reserved flags */
#define OSTAT	0x00		/* open for stat/wstat */
#define OREAD	0x01		/* open for read */
#define OWRITE	0x02		/* write */
#define ORDWR	(OREAD|OWRITE)	/* read and write */
#define OEXEC	0x04		/* execute, == read but check execute permission */
#define OCEXEC	0x08		/* or'ed in, close on exec */
#define ORCLOSE	0x10		/* or'ed in, remove on close */
#define OKMODE	0xff		/* least significant byte reserved for kernel use */

/* Open modes: Popular flags among filesystems */
#define OTRUNC	0x0100		/* or'ed in (except for exec), truncate file first */

/* Access modes */
#define	AEXIST	0	/* accessible: exists */
#define	AREAD	4	/* read ccess */
#define	AWRITE	2	/* write access */
#define	AEXEC	1	/* execute access */
#define AMASK	(AEXIST|AREAD|AWRITE|AEXEC)

/* Segattch */
#define	SG_RONLY	0040	/* read only */
#define	SG_CEXEC	0100	/* detach on exec */

#define	NCONT	0	/* continue after note */
#define	NDFLT	1	/* terminate after note */
#define	NSAVE	2	/* clear note but hold state */
#define	NRSTR	3	/* restore saved state */

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTAUTH		0x08		/* type bit for authentication file */
#define QTTMP		0x04		/* type bit for not-backed-up file */
#define QTFILE		0x00		/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMAUTH		0x08000000	/* mode bit for authentication file */
#define DMTMP		0x04000000	/* mode bit for non-backed-up files */
#define DMREAD		0x4		/* mode bit for read permission */
#define DMWRITE		0x2		/* mode bit for write permission */
#define DMEXEC		0x1		/* mode bit for execute permission */

/* rfork */
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
	RFNOMNT		= (1<<14)
};

typedef
struct Qid
{
	uint64_t	path;
	uint32_t	vers;
	uint8_t	type;
} Qid;

typedef
struct Dir {
	/* system-modified data */
	uint16_t	type;	/* server type */
	uint32_t	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
	uint32_t	mode;	/* permissions */
	uint32_t	atime;	/* last read time */
	uint32_t	mtime;	/* last write time */
	int64_t	length;	/* file length */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
} Dir;

/* keep /sys/src/ape/lib/ap/plan9/sys9.h in sync with this -rsc */
typedef
struct Waitmsg
{
	pid_t		pid;		/* of loved one */
	uint32_t	time[3];	/* of loved one & descendants */
	char		msg[];
} Waitmsg;

extern	int	jehanne_access(const char*, int);
extern	int	jehanne_awakened(int64_t);
extern	int	jehanne_brk(void*);
extern	int	jehanne_execl(const char*, ...);
extern	int	jehanne_forgivewkp(int64_t);
extern	pid_t	jehanne_fork(void);
extern	int	jehanne_pexec(const char* cmd, char* args[]);
extern	int32_t	jehanne_readn(int, void*, int32_t);
extern	void*	jehanne_segbrk(uint32_t);
#define sbrk(incr) jehanne_segbrk(incr)
extern	void*	jehanne_segattach(int, const char*, void*, unsigned long);
extern	int	jehanne_segdetach(void*);
extern	int	jehanne_segfree(void*, unsigned long);
extern	void	jehanne_sleep(unsigned int millisecs);
extern	int	jehanne_stat(const char*, uint8_t*, int);
extern	Waitmsg*	jehanne_wait(void);
extern	int	jehanne_waitpid(void);
extern	int	jehanne_wstat(const char*, uint8_t*, int);

extern	long	jehanne_read(int, void*, int32_t);
extern	long	jehanne_write(int, const void*, int32_t);

extern	Dir*	jehanne_dirstat(const char*);
extern	Dir*	jehanne_dirfstat(int);
extern	int	jehanne_dirwstat(const char*, Dir*);
extern	int	jehanne_dirfwstat(int, Dir*);
extern	int32_t	jehanne_dirread(int, Dir**);
extern	void	jehanne_nulldir(Dir*);
extern	int32_t	jehanne_dirreadall(int, Dir**);
extern	int32_t	jehanne_getpid(void);
extern	int32_t	jehanne_getppid(void);
extern	int32_t	jehanne_getmainpid(void);
extern	void	jehanne_rerrstr(char*, uint32_t);
extern	char*	jehanne_sysname(void);
extern	void	jehanne_werrstr(const char*, ...);

extern unsigned int	jehanne_convM2D(uint8_t*, uint, Dir*, char*);
extern unsigned int	jehanne_convD2M(Dir*, uint8_t*, uint);
extern unsigned int	jehanne_sizeD2M(Dir*);
extern int		jehanne_dirmodefmt(Fmt*);

#ifndef NPRIVATES
# define NPRIVATES	16
#endif

/* compiler directives on plan 9 */
#define SET(x)  ((x)=0)
#define USED(x) if(x){}else{}
#ifdef __GNUC__
#       if __GNUC__ >= 3
#               undef USED
#               define USED(x) ((void)(x))
#       endif
#endif

extern char *argv0;
/* #define	ARGBEGIN	for((argv0||(argv0=*argv)),argv++,argc--;\ */
#define ARGBEGIN        if(argv0==nil){\
				argv0=*argv;\
			}\
			for(argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt;\
				Rune _argc;\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				_argc = 0;\
				while(*_args && (_args += jehanne_chartorune(&_argc, _args)))\
				switch(_argc)
/* #define	ARGEND		SET(_argt);USED(_argt,_argc,_args);}USED(argv, argc); */
#define ARGEND          SET(_argt);USED(_argt);USED(_argc);USED(_args);}USED(argv);USED(argc);
#define	ARGF()		(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): 0))
#define	EARGF(x)	(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): ((x), argc = *(int*)0, (char*)0)))

#define	ARGC()		_argc

/* this is used by segbrk and brk,  it's a really bad idea to redefine it */
extern	char	end[];
