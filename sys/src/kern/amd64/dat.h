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
typedef struct BIOS32si	BIOS32si;
typedef struct BIOS32ci	BIOS32ci;
typedef struct Fxsave Fxsave;
typedef struct IOConf IOConf;
typedef struct ISAConf ISAConf;
typedef struct Label Label;
typedef struct Lock Lock;
typedef struct LockEntry LockEntry;
typedef struct MCPU MCPU;
typedef struct MFPU MFPU;
typedef struct MMMU MMMU;
typedef struct Mach Mach;
typedef uint64_t Mpl;
typedef Mpl Mreg;				/* GAK */
typedef struct Ptpage Ptpage;
typedef struct Pcidev Pcidev;
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;
typedef struct PNOTIFY PNOTIFY;
typedef struct PPAGE PPAGE;
typedef uint64_t PTE;
typedef struct Proc Proc;
typedef struct Sys Sys;
typedef uint64_t uintmem;			/* horrible name */
typedef struct Ureg Ureg;
typedef struct Vctl Vctl;

#pragma incomplete BIOS32si
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */

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
	LockEntry*	head;
	LockEntry*	e;
};

struct Label
{
	uintptr_t	sp;
	uintptr_t	pc;
	uintptr_t	regs[14];
};

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

/*
 *  FPU stuff in Proc
 */
struct PFPU {
	int		fpustate;
	uint8_t		fxsave[sizeof(Fxsave)+15];
	void*		fpusave;
};

struct Ptpage
{
	PTE*		pte;		/* kernel-addressible page table entries */
	uintmem		pa;		/* physical address (from physalloc) */
	Ptpage*		next;		/* next in level's set, or free list */
	Ptpage*		parent;		/* parent page table page or page directory */
	uint32_t	ptoff;		/* index of this table's entry in parent */
};

/*
 *  MMU stuff in Proc
 */
struct PMMU
{
	Ptpage*		mmuptp[4];	/* page table pages for each level */
	Ptpage*		ptpfree;
	int		nptpbusy;
};

/*
 * MMU stuff in Page
 */
struct PPAGE
{
	uint8_t*	nothing;
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

#define MAXMDOM	8	/* maximum memory/cpu domains */

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
enum
{
	NPGSZ = 4
};

struct MMMU
{
	Ptpage*		pml4;			/* pml4 for this processor */
	PTE*		pmap;			/* unused as of yet */
	Ptpage*		ptpfree;		/* per-mach free list */
	int		nptpfree;

	uint32_t	pgszlg2[NPGSZ];		/* per Mach or per Sys? */
	uintmem		pgszmask[NPGSZ];
	uint32_t	pgsz[NPGSZ];
	int		npgsz;

	Ptpage		pml4kludge;
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

	int		apicno;
	int		online;
	int		mode;		/* fold into online? GAK */

	void*		dbgreg;		/* registers for debugging this processor */
	void*		dbgsp;		/* sp for debugging this processor */

	MMMU;

	uintptr_t	stack;
	uint8_t*	vsvm;
	void*		gdt;
	void*		tss;

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

	Lock		apictimerlock;
	uint64_t	cyclefreq;	/* Frequency of user readable cycle counter */
	int64_t		cpuhz;
	int		cpumhz;
	uint64_t	rdtsc;

	LockEntry	locks[8];

	MFPU	FPU;
	MCPU;
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
struct Sys {
	uint8_t	machstk[MACHSTKSZ];

	PTE	pml4[PTSZ/sizeof(PTE)];	/*  */
	PTE	pdp[PTSZ/sizeof(PTE)];
	PTE	pd[PTSZ/sizeof(PTE)];
	PTE	pt[PTSZ/sizeof(PTE)];

	uint8_t	vsvmpage[4*KiB];

	union {
		Mach	mach;
		uint8_t	machpage[MACHSZ];
	};

	union {
		struct {
			uint64_t	pmstart;	/* physical memory */
			uint64_t	pmoccupied;	/* how much is occupied */
			uint64_t	pmunassigned;	/* how much to keep back  from page pool */
			uint64_t	pmpaged;	/* how much assigned to page pool */

			uintptr_t	vmstart;	/* base address for malloc */
			uintptr_t	vmunused;	/* 1st unused va */
			uintptr_t	vmunmapped;	/* 1st unmapped va in KSEG0 */

			uint64_t	epoch;		/* crude time synchronisation */
			int		nmach;		/* how many machs */
			int		nonline;	/* how many machs are online */
			uint64_t	ticks;		/* since boot (type?) */
			uint32_t	copymode;	/* 0=copy on write; 1=copy on reference */
		};
		uint8_t	syspage[4*KiB];
	};

	union {
		Mach*	machptr[MACHMAX];
		uint8_t	ptrpage[4*KiB];
	};

	uint8_t	_57344_[2][4*KiB];		/* unused */
};

extern Sys* sys;

/*
 * KMap
 */
typedef void KMap;

#define kunmap(k)
#define VA(k)		(void*)(k)

struct
{
	Lock;
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

#define dbgprint	print		/* for now */
