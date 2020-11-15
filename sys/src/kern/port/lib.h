/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015 Ronald G. Minnich <rminnich@gmail.com>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

/*
 * functions (possibly) linked in, complete, from libc.
 */
#define nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define offsetof(s, m)	(uintptr_t)(&(((s*)0)->m))
#define assert(x)	if(x){}else jehanne__assert(#x)
#define getcallerpc()	((uintptr_t)__builtin_return_address(0))
extern	void	jehanne__assert(const char*) __attribute__ ((noreturn));

/*
 * mem routines
 */
extern	void*	jehanne_memccpy(void*, void*, int, uint32_t);
extern	void*	jehanne_memset(void*, int, uint32_t);
extern	int	jehanne_memcmp(void*, void*, uint32_t);
extern	void*	jehanne_memmove(void*, void*, uint32_t);
extern	void*	jehanne_memchr(void*, int, uint32_t);

/*
 * string routines
 */
extern	char*	jehanne_strcat(char*, char*);
extern	char*	jehanne_strchr(char*, int);
extern	int	jehanne_strcmp(char*, char*);
extern	char*	jehanne_strcpy(char*, char*);
extern	char*	jehanne_strecpy(char*, char*, char*);
extern	char*	jehanne_strncat(char*, char*, long);
extern	char*	jehanne_strncpy(char*, char*, long);
extern	int	jehanne_strncmp(char*, char*, long);
extern	char*	jehanne_strrchr(char*, int);
extern	long	jehanne_strlen(char*);
extern	char*	jehanne_strstr(char*, char*);
extern	int	jehanne_cistrncmp(char*, char*, int);
extern	int	jehanne_cistrcmp(char*, char*);
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
extern	int	jehanne_runetochar(char*, Rune*);
extern	int	jehanne_chartorune(Rune*, char*);
extern	int	jehanne_runelen(long);
extern	int	jehanne_fullrune(char*, int);
extern	int	jehanne_utflen(char*);
extern	int	jehanne_utfnlen(char*, long);
extern	char*	jehanne_utfrune(char*, long);

/*
 * malloc
 */
extern	void*	jehanne_malloc(usize);
extern	void*	jehanne_mallocz(usize, int);
extern	void	jehanne_free(void*);
extern	uint32_t	jehanne_msize(void*);
extern	void*	jehanne_mallocalign(usize, uint32_t, long, uint32_t);
extern	void*	jehanne_realloc(void*, usize);
void	jehanne_setmalloctag(void*, uintptr_t);
void	jehanne_setrealloctag(void*, uintptr_t);
uintptr_t	jehanne_getmalloctag(void*);
uintptr_t	jehanne_getrealloctag(void*);

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

enum {
	FmtWidth	= 1,
	FmtLeft		= FmtWidth<<1,
	FmtPrec		= FmtLeft<<1,
	FmtSharp	= FmtPrec<<1,
	FmtSpace	= FmtSharp<<1,
	FmtSign		= FmtSpace<<1,
	FmtZero		= FmtSign<<1,
	FmtUnsigned	= FmtZero<<1,
	FmtShort	= FmtUnsigned<<1,
	FmtLong		= FmtShort<<1,
	FmtVLong	= FmtLong<<1,
	FmtComma	= FmtVLong<<1,
	FmtByte		= FmtComma<<1,

	FmtFlag		= FmtByte<<1
};

extern	int	jehanne_print(char*, ...);
extern	char*	jehanne_seprint(char*, char*, char*, ...);
extern	char*	jehanne_vseprint(char*, char*, char*, va_list);
extern	int	jehanne_snprint(char*, int, char*, ...);
extern	int	jehanne_vsnprint(char*, int, char*, va_list);
extern	int	jehanne_sprint(char*, char*, ...);



#pragma	varargck	argpos	fmtprint	2
#pragma	varargck	argpos	print		1
#pragma	varargck	argpos	seprint		3
#pragma	varargck	argpos	snprint		3
#pragma	varargck	argpos	sprint		2

#pragma	varargck	type	"lld"	int64_t
#pragma	varargck	type	"llx"	int64_t
#pragma	varargck	type	"lld"	uint64_t
#pragma	varargck	type	"llx"	uint64_t
#pragma	varargck	type	"ld"	long
#pragma	varargck	type	"lx"	long
#pragma	varargck	type	"ld"	uint32_t
#pragma	varargck	type	"lx"	uint32_t
#pragma	varargck	type	"d"	int
#pragma	varargck	type	"x"	int
#pragma	varargck	type	"c"	int
#pragma	varargck	type	"C"	int
#pragma	varargck	type	"d"	uint32_t
#pragma	varargck	type	"x"	uint32_t
#pragma	varargck	type	"c"	uint32_t
#pragma	varargck	type	"C"	uint32_t
#pragma	varargck	type	"s"	char*
#pragma	varargck	type	"q"	char*
#pragma	varargck	type	"S"	Rune*
#pragma	varargck	type	"%"	void
#pragma	varargck	type	"p"	uintptr_t
#pragma	varargck	type	"p"	void*
#pragma	varargck	flag	','
#pragma varargck	type	"<"	void*
#pragma varargck	type	"["	void*
#pragma varargck	type	"H"	void*
#pragma varargck	type	"lH"	void*

extern	int	jehanne_fmtinstall(int, int (*)(Fmt*));
extern	int	jehanne_fmtprint(Fmt*, char*, ...);
extern	int	jehanne_fmtstrcpy(Fmt*, char*);
extern	char*	jehanne_fmtstrflush(Fmt*);
extern	int	jehanne_fmtstrinit(Fmt*);

/*
 * quoted strings
 */
extern	void	jehanne_quotefmtinstall(void);

/*
 * one-of-a-kind
 */
extern	int	jehanne_abs(int);
extern	int	jehanne_atoi(char*);
extern	char*	jehanne_cleanname(char*);
extern	int	jehanne_dec16(uint8_t*, int, char*, int);
extern	int	jehanne_enc16(char*, int, uint8_t*, int);
extern	int	jehanne_encodefmt(Fmt*);
extern	int	jehanne_dec64(uint8_t*, int, char*, int);
//#define	getcallerpc()	((uintptr_t)__builtin_return_address(0))
extern	int	jehanne_getfields(char*, char**, int, int, char*);
extern	int	jehanne_gettokens(char *, char **, int, char *);
extern	void	jehanne_qsort(void*, long, long, int (*)(void*, void*));
extern	long	jehanne_strtol(char*, char**, int);
extern	uint32_t	jehanne_strtoul(char*, char**, int);
extern	int64_t	jehanne_strtoll(char*, char**, int);
extern	uint64_t	jehanne_strtoull(char*, char**, int);

/*
 * Syscall data structures
 */
#define MORDER	0x0003	/* mask for bits defining order of mounting */
#define MREPL	0x0000	/* mount replaces object */
#define MBEFORE	0x0001	/* mount goes before others in union directory */
#define MAFTER	0x0002	/* mount goes after others in union directory */
#define MCREATE	0x0004	/* permit creation in mounted directory */
#define MMASK	0x0007	/* all bits on */

/* OPEN MODES: Kernel reserved flags */
#define OSTAT	0x00		/* open for stat/wstat */
#define OREAD	0x01		/* open for read */
#define OWRITE	0x02		/* write */
#define ORDWR	(OREAD|OWRITE)	/* read and write */
#define OEXEC	0x04		/* execute, == read but check execute permission */
#define OCEXEC	0x08		/* or'ed in, close on exec */
#define ORCLOSE	0x10		/* or'ed in, remove on close */
#define OKMODE	0xff		/* least significant byte reserved for kernel use */

/* OPEN MODES: Popular flags among filesystems */
#define OTRUNC	0x0100		/* or'ed in (except for exec), truncate file first */

#define NCONT	0	/* continue after note */
#define NDFLT	1	/* terminate after note */
#define NSAVE	2	/* clear note but hold state */
#define NRSTR	3	/* restore saved state */

typedef struct Qid	Qid;
typedef struct Dir	Dir;
typedef struct OWaitmsg	OWaitmsg;
typedef struct Waitmsg	Waitmsg;

#define ERRMAX		128		/* max length of error string */
#define KNAMELEN	28		/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTAUTH		0x08		/* type bit for authentication file */
#define QTFILE		0x00		/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMREAD		0x4		/* mode bit for read permission */
#define DMWRITE		0x2		/* mode bit for write permission */
#define DMEXEC		0x1		/* mode bit for execute permission */

struct Qid
{
	uint64_t	path;
	uint32_t	vers;
	uint8_t		type;
};

struct Dir {
	/* system-modified data */
	uint16_t	type;	/* server type */
	uint32_t	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
	uint32_t	mode;	/* permissions */
	uint32_t	atime;	/* last read time */
	uint32_t	mtime;	/* last write time */
	int64_t	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
};

struct OWaitmsg
{
	char	pid[12];	/* of loved one */
	char	time[3*12];	/* of loved one and descendants */
	char	msg[64];	/* compatibility BUG */
};

struct Waitmsg
{
	int	pid;		/* of loved one */
	uint32_t	time[3];	/* of loved one and descendants */
	char	msg[ERRMAX];	/* actually variable-size in user mode */
};

typedef
struct IOchunk
{
	void	*addr;
	uint32_t	len;
} IOchunk;

extern	char	etext[];
extern	char	edata[];
extern	char	end[];

extern	char*	jehanne_smprint(char*, ...);
extern	char*	jehanne_strdup(char*);


extern unsigned int	jehanne_convM2D(uint8_t*, uint, Dir*, char*);
extern unsigned int	jehanne_convD2M(Dir*, uint8_t*, uint);
extern unsigned int	jehanne_sizeD2M(Dir*);
extern int		dirfmt(Fmt*);
extern int		jehanne_dirmodefmt(Fmt*);
#pragma	varargck	type	"D"	Dir*

#define memccpy jehanne_memccpy
#define memset jehanne_memset
#define memcmp jehanne_memcmp
#define memcpy jehanne_memcpy
#define memmove jehanne_memmove
#define memchr jehanne_memchr
#define strcat jehanne_strcat
#define strchr jehanne_strchr
#define strcmp jehanne_strcmp
#define strcpy jehanne_strcpy
#define strecpy jehanne_strecpy
#define strdup jehanne_strdup
#define strncat jehanne_strncat
#define strncpy jehanne_strncpy
#define strncmp jehanne_strncmp
#define strpbrk jehanne_strpbrk
#define strrchr jehanne_strrchr
#define strtok jehanne_strtok
#define strlen jehanne_strlen
#define strspn jehanne_strspn
#define strcspn jehanne_strcspn
#define strstr jehanne_strstr
#define cistrncmp jehanne_cistrncmp
#define cistrcmp jehanne_cistrcmp
#define cistrstr jehanne_cistrstr
#define tokenize jehanne_tokenize
#define runetochar jehanne_runetochar
#define chartorune jehanne_chartorune
#define runelen jehanne_runelen
#define runenlen jehanne_runenlen
#define fullrune jehanne_fullrune
#define utflen jehanne_utflen
#define utfnlen jehanne_utfnlen
#define utfrune jehanne_utfrune
#define utfrrune jehanne_utfrrune
#define utfutf jehanne_utfutf
#define utfecpy jehanne_utfecpy
#define runestrcat jehanne_runestrcat
#define runestrchr jehanne_runestrchr
#define runestrcmp jehanne_runestrcmp
#define runestrcpy jehanne_runestrcpy
#define runestrncpy jehanne_runestrncpy
#define runestrecpy jehanne_runestrecpy
#define runestrdup jehanne_runestrdup
#define runestrncat jehanne_runestrncat
#define runestrncmp jehanne_runestrncmp
#define runestrrchr jehanne_runestrrchr
#define runestrlen jehanne_runestrlen
#define runestrstr jehanne_runestrstr
#define tolowerrune jehanne_tolowerrune
#define totitlerune jehanne_totitlerune
#define toupperrune jehanne_toupperrune
#define tobaserune jehanne_tobaserune
#define isalpharune jehanne_isalpharune
#define isbaserune jehanne_isbaserune
#define isdigitrune jehanne_isdigitrune
#define islowerrune jehanne_islowerrune
#define isspacerune jehanne_isspacerune
#define istitlerune jehanne_istitlerune
#define isupperrune jehanne_isupperrune
#define malloc jehanne_malloc
#define malloc jehanne_malloc
#define mallocz jehanne_mallocz
#define free jehanne_free
#define msize jehanne_msize
#define mallocalign jehanne_mallocalign
#define calloc jehanne_calloc
#define realloc jehanne_realloc
#define setmalloctag jehanne_setmalloctag
#define setrealloctag jehanne_setrealloctag
#define getmalloctag jehanne_getmalloctag
#define getrealloctag jehanne_getrealloctag
#define malloctopoolblock jehanne_malloctopoolblock
#define print jehanne_print
#define seprint jehanne_seprint
#define vseprint jehanne_vseprint
#define snprint jehanne_snprint
#define vsnprint jehanne_vsnprint
#define smprint jehanne_smprint
#define vsmprint jehanne_vsmprint
#define sprint jehanne_sprint
#define fprint jehanne_fprint
#define vfprint jehanne_vfprint
#define runesprint jehanne_runesprint
#define runesnprint jehanne_runesnprint
#define runevsnprint jehanne_runevsnprint
#define runeseprint jehanne_runeseprint
#define runevseprint jehanne_runevseprint
#define runesmprint jehanne_runesmprint
#define runevsmprint jehanne_runevsmprint
#define fmtfdinit jehanne_fmtfdinit
#define fmtfdflush jehanne_fmtfdflush
#define fmtstrinit jehanne_fmtstrinit
#define fmtstrflush jehanne_fmtstrflush
#define runefmtstrinit jehanne_runefmtstrinit
#define runefmtstrflush jehanne_runefmtstrflush
#define fmtinstall jehanne_fmtinstall
#define dofmt jehanne_dofmt
#define dorfmt jehanne_dorfmt
#define fmtprint jehanne_fmtprint
#define fmtvprint jehanne_fmtvprint
#define fmtrune jehanne_fmtrune
#define fmtstrcpy jehanne_fmtstrcpy
#define fmtrunestrcpy jehanne_fmtrunestrcpy
#define errfmt jehanne_errfmt
#define unquotestrdup jehanne_unquotestrdup
#define unquoterunestrdup jehanne_unquoterunestrdup
#define quotestrdup jehanne_quotestrdup
#define quoterunestrdup jehanne_quoterunestrdup
#define quotestrfmt jehanne_quotestrfmt
#define quoterunestrfmt jehanne_quoterunestrfmt
#define quotefmtinstall jehanne_quotefmtinstall
#define needsrcquote jehanne_needsrcquote
#define srand jehanne_srand
#define rand jehanne_rand
#define nrand jehanne_nrand
#define lrand jehanne_lrand
#define lnrand jehanne_lnrand
#define frand jehanne_frand
#define truerand jehanne_truerand
#define ntruerand jehanne_ntruerand
#define getfcr jehanne_getfcr
#define setfsr jehanne_setfsr
#define setfcr jehanne_setfcr
#define NaN jehanne_NaN
#define Inf jehanne_Inf
#define isNaN jehanne_isNaN
#define isInf jehanne_isInf
#define umuldiv jehanne_umuldiv
#define muldiv jehanne_muldiv
#define pow jehanne_pow
#define atan2 jehanne_atan2
#define fabs jehanne_fabs
#define atan jehanne_atan
#define log jehanne_log
#define log10 jehanne_log10
#define exp jehanne_exp
#define floor jehanne_floor
#define ceil jehanne_ceil
#define hypot jehanne_hypot
#define sin jehanne_sin
#define cos jehanne_cos
#define tan jehanne_tan
#define asin jehanne_asin
#define acos jehanne_acos
#define sinh jehanne_sinh
#define cosh jehanne_cosh
#define tanh jehanne_tanh
#define sqrt jehanne_sqrt
#define atof jehanne_atof
#define atoi jehanne_atoi
#define atol jehanne_atol
#define atoll jehanne_atoll
#define dec64 jehanne_dec64
#define enc64 jehanne_enc64
#define dec32 jehanne_dec32
#define enc32 jehanne_enc32
#define dec16 jehanne_dec16
#define enc16 jehanne_enc16
#define encodefmt jehanne_encodefmt
#define getfields jehanne_getfields
#define gettokens jehanne_gettokens
#define qsort jehanne_qsort
#define strtod jehanne_strtod
#define strtol jehanne_strtol
#define strtoul jehanne_strtoul
#define strtoll jehanne_strtoll
#define strtoull jehanne_strtoull
#define access jehanne_access
#define convM2D jehanne_convM2D
#define convD2M jehanne_convD2M
#define sizeD2M jehanne_sizeD2M
#define chartorune jehanne_chartorune
