/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2017 Giacomo Tesio <giacomo@tesio.it>
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
typedef struct BIOS32si	BIOS32si;
typedef struct BIOS32ci	BIOS32ci;
typedef struct Confmem Confmem;
typedef struct Fxsave Fxsave;
typedef struct IOConf IOConf;
typedef struct ISAConf ISAConf;
typedef struct Label Label;
typedef struct Lock Lock;
typedef struct LockEntry LockEntry;
typedef struct MCPU MCPU;
typedef struct MFPU MFPU;
typedef struct MMU MMU;
typedef struct MMMU MMMU;
typedef struct Mach Mach;
typedef uint64_t Mpl;
typedef Mpl Mreg;				/* GAK */
typedef struct Segdesc Segdesc;
typedef struct Pcidev Pcidev;
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;
typedef struct PNOTIFY PNOTIFY;
typedef struct PPAGE PPAGE;
typedef uint64_t PTE;
typedef struct RMap RMap;
typedef struct Proc Proc;
typedef struct Sys Sys;
typedef uint64_t uintmem;			/* horrible name */
typedef long Tval;
typedef struct Ureg Ureg;
typedef struct Vctl Vctl;
typedef struct PCArch PCArch;
typedef union  FPsave	FPsave;
typedef struct Fxsave	Fxsave;
typedef struct FPstate	FPstate;
typedef struct PCMmap	PCMmap;

#pragma incomplete BIOS32si
#pragma incomplete Ureg

#define MAXSYSARG	6	/* for mount(fd, afd, mpt, flag, arg, dc) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(S_MAGIC)
#define ELF_MAGIC	(ELF_MAG)

/*
 *  machine dependent definitions used by ../port/portdat.h
 */
struct Lock
{
	uint64_t	sr;
	uintptr_t	pc;
	Proc		*lp;
	Mach		*lm;
	uint32_t	key;
	uint16_t	isilock;
	long		lockcycles;
};


struct Label
{
	uintptr_t	sp;
	uintptr_t	pc;
	uintptr_t	regs[14];
};

/*
 * FPsave.status
 */
enum
{
	/* this is a state */
	FPinit=		0,
	FPactive=	1,
	FPinactive=	2,

	/* the following is a bit that can be or'd into the state */
	FPillegal=	0x100,
};

/*
 * the FP regs must be stored here, not somewhere pointed to from here.
 * port code assumes this.
 */
struct Fxsave {
	uint16_t	fcw;		/* x87 control word */
	uint16_t	fsw;		/* x87 status word */
	uint8_t		ftw;		/* x87 tag word */
	uint8_t		zero;		/* 0 */
	uint16_t	fop;		/* last x87 opcode */
	uint64_t	rip;		/* last x87 instruction pointer */
	uint64_t	rdp;		/* last x87 data pointer */
	uint32_t	mxcsr;		/* MMX control and status */
	uint32_t	mxcsrmask;	/* supported MMX feature bits */
	uint8_t		st[128];	/* shared 64-bit media and x87 regs */
	uint8_t		xmm[256];	/* 128-bit media regs */
	uint8_t		ign[96];	/* reserved, ignored */
};

union FPsave {
	uint8_t align[512+15];
	Fxsave;
};

struct Segdesc
{
	uint32_t	d0;
	uint32_t	d1;
};

/*
 *  MMU structure for PDP, PD, PT pages.
 */
struct MMU
{
	MMU*		next;
	uintptr_t*	page;
	int		index;
	int		level;
};

/*
 *  MMU stuff in Proc
 */
#define NCOLOR 1
struct PMMU
{
	MMU*		mmuhead;
	MMU*		mmutail;
	MMU*		kmaphead;
	MMU*		kmaptail;
	unsigned long	kmapcount;
	unsigned long	kmapindex;
	unsigned long	mmucount;
};

/*
 *  things saved in the Proc structure during a notify
 */
struct PNOTIFY
{
	char		emptiness;
};

struct IOConf
{
	int		nomsi;
	int		nolegacyprobe;	/* acpi tells us.  all negated in case acpi unavailable */
	int		noi8042kbd;
	int		novga;
	int		nocmos;
};
extern IOConf ioconf;

#include "../port/portdat.h"

/*
 *  CPU stuff in Mach.
 */
struct MCPU {
	uint32_t	cpuinfo[2][4];		/* CPUID instruction output E[ABCD]X */
	int		ncpuinfos;		/* number of standard entries */
	int		ncpuinfoe;		/* number of extended entries */
	int		isintelcpu;		/*  */
};

/*
 *  FPU stuff in Mach.
 */
struct MFPU {
	uint16_t	fcw;			/* x87 control word */
	uint32_t	mxcsr;			/* MMX control and status */
	uint32_t	mxcsrmask;		/* supported MMX feature bits */
};

/*
 *  MMU stuff in Mach.
 */
typedef struct {
	uint32_t	_0_;
	uint32_t	rsp0[2];
	uint32_t	rsp1[2];
	uint32_t	rsp2[2];
	uint32_t	_28_[2];
	uint32_t	ist[14];
	uint16_t	_92_[5];
	uint16_t	iomap;
} Tss;

struct MMMU
{
	uint64_t*	pml4;		/* pml4 base for this processor (va) */
	Tss*		tss;		/* tss for this processor */
	Segdesc*	gdt;		/* gdt for this processor */

	uint64_t	mmumap[4];	/* bitmap of pml4 entries for zapping */
	MMU*		mmufree;	/* freelist for MMU structures */
	unsigned long	mmucount;	/* number of MMU structures in freelist */
};

/*
 * Per processor information.
 *
 * The offsets of the first few elements may be known
 * to low-level assembly code, so do not re-order:
 *	machno	- no dependency, convention
 *	splpc	- splhi, spllo, splx
 *	proc	- syscallentry
 */
struct Mach
{
	uint64_t	machno;		/* physical id of processor */
	uint64_t	splpc;		/* pc of last caller to splhi */
	Proc*		proc;		/* current process on this processor */
	uint64_t	tmp0;		/* for syscallentry */
	uint64_t	tmp1;		/* for syscallentry */

	int		apicno;
	int		online;
	int		mode;		/* fold into online? GAK */

	void*		dbgreg;		/* registers for debugging this processor */
	void*		dbgsp;		/* sp for debugging this processor */

	MMMU;

	uintptr_t	stack;
	uint8_t*	vsvm;

	uint64_t	ticks;		/* of the clock since boot time */
	Label		sched;		/* scheduler wakeup */
	Lock		alarmlock;	/* access to alarm list */
	void*		alarm;		/* alarms bound to this clock */
	int		inclockintr;

	Proc*		readied;	/* for runproc */
	uint64_t	schedticks;	/* next forced context switch */

	int		color;

	int		tlbfault;
	int		tlbpurge;
	int		pfault;
	int		cs;
	int		syscall;
	int		load;
	int		intr;
	int		mmuflush;	/* make current proc flush it's mmu state */
	int		ilockdepth;
	uintptr_t	ilockpc;
	Perf		perf;		/* performance counters */
	void		(*perfintr)(Ureg*, void*);

	int		lastintr;

	int		loopconst;

	Lock		apictimerlock;
	uint64_t	cyclefreq;	/* Frequency of user readable cycle counter */
	uint64_t	cpuhz;
	int		cpumhz;
	int		cpuidax;
	int		cpuidcx;
	int		cpuiddx;
	char		cpuidid[16];
	char*		cpuidtype;
	int		havetsc;
	int		havepge;
	uint64_t	tscticks;
	uint64_t	rdtsc;

//	LockEntry	locks[8];

	MFPU	FPU;
	MCPU;
};

struct Confmem
{
	uintptr_t	base;
	unsigned long	npage;
	uintptr_t	kbase;
	uintptr_t	klimit;
};

/*
 * This is the low memory map, between 0x100000 and 0x110000.
 * It is located there to allow fundamental datastructures to be
 * created and used before knowing where free memory begins
 * (e.g. there may be modules located after the kernel BSS end).
 * The layout is known in the bootstrap code in l32p.s.
 * It is logically two parts: the per processor data structures
 * for the bootstrap processor (stack, Mach, vsvm, and page tables),
 * and the global information about the system (syspage, ptrpage).
 * Some of the elements must be aligned on page boundaries, hence
 * the unions.
 */
struct Sys
{
	Ureg*		boot_regs;
	unsigned int	nmach;		/* processors */
	unsigned int	nproc;		/* processes */
	unsigned int	monitor;	/* has monitor? */
	unsigned int	npages;		/* total physical pages of memory */
	unsigned int	upages;		/* user page pool */
	unsigned int	nimage;		/* number of images */
	Confmem		mem[16];	/* physical memory */

	unsigned int	copymode;	/* 0 is copy on write, 1 is copy on reference */
	unsigned int	ialloc;		/* max interrupt time allocation in bytes */
	unsigned int	pipeqsize;	/* size in bytes of pipe queues */
	int		nuart;		/* number of uart devices */

	char*		architecture;
	uint64_t	ticks;
	Mach*		machptr[MACHMAX];

	uint64_t	pmstart;	/* physical memory */
	uint64_t	pmoccupied;	/* how much is occupied */
	uint64_t	pmunassigned;	/* how much to keep back  from page pool */
	uint64_t	pmpaged;	/* how much assigned to page pool */
};

extern Sys* sys;
extern uint32_t	MemMin;		/* set by entry.S */

/*
 * KMap
 */
typedef void KMap;

#define VA(k)		(void*)(k)

/*
 *  routines for things outside the PC model, like power management
 */
struct PCArch
{
	char*		id;
	int		(*ident)(void);		/* this should be in the model */
	void		(*reset)(void);		/* this should be in the model */
	int		(*serialpower)(int);	/* 1 == on, 0 == off */
	int		(*modempower)(int);	/* 1 == on, 0 == off */

	void		(*intrinit)(void);
	int		(*intrenable)(Vctl*);
	int		(*intrvecno)(int);
	int		(*intrdisable)(int);
	void		(*introff)(void);
	void		(*intron)(void);

	void		(*clockenable)(void);
	uint64_t	(*fastclock)(uint64_t*);
	void		(*timerset)(uint64_t);
};

extern PCArch	*arch;			/* PC architecture */

/* cpuid instruction result register bits */
enum {
	/* cx */
	Monitor	= 1<<3,

	/* dx */
	Fpuonchip = 1<<0,
	Vmex	= 1<<1,		/* virtual-mode extensions */
	Pse	= 1<<3,		/* page size extensions */
	Tsc	= 1<<4,		/* time-stamp counter */
	Cpumsr	= 1<<5,		/* model-specific registers, rdmsr/wrmsr */
	Pae	= 1<<6,		/* physical-addr extensions */
	Mce	= 1<<7,		/* machine-check exception */
	Cmpxchg8b = 1<<8,
	Cpuapic	= 1<<9,
	Mtrr	= 1<<12,	/* memory-type range regs.  */
	Pge	= 1<<13,	/* page global extension */
	Mca	= 1<<14,	/* machine-check architecture */
	Pat	= 1<<16,	/* page attribute table */
	Pse2	= 1<<17,	/* more page size extensions */
	Clflush = 1<<19,
	Acpif	= 1<<22,	/* therm control msr */
	Mmx	= 1<<23,
	Fxsr	= 1<<24,	/* have SSE FXSAVE/FXRSTOR */
	Sse	= 1<<25,	/* thus sfence instr. */
	Sse2	= 1<<26,	/* thus mfence & lfence instr.s */
	Rdrnd	= 1<<30,	/* RDRAND support bit */
};

/* Informations about active processors for
 * bootstrap and shutdown.
 */
struct
{
	char	machs[MACHMAX];		/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
	int	thunderbirdsarego;	/* F.A.B. */
}active;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char*		type;
	uintptr_t	port;
	int		irq;
	uint32_t	dma;
	uintptr_t	mem;
	usize		size;
	uint32_t	freq;
	int		tbdf;		/* type+busno+devno+funcno */

	int		nopt;
	char*		opt[NISAOPT];
};

typedef struct BIOS32ci {		/* BIOS32 Calling Interface */
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	esi;
	uint32_t	edi;
} BIOS32ci;

#define	MACHP(n)	(sys->machptr[n])

/*
 * The Mach structures must be available via the per-processor
 * MMU information array machptr, mainly for disambiguation and access to
 * the clock which is only maintained by the bootstrap processor (0).
 */
register Mach* m asm("r15");
register Proc* up asm("r14");

/*
 * Horrid.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

#define DBG(...)	if(!DBGFLG){}else dbgprint(__VA_ARGS__)

extern char dbgflg[256];

#define dbgprint	jehanne_print		/* for now */
