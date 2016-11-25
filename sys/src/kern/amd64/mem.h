/*
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */

/*
 * unfortunately, GAS does not accept [u]l* suffixes, then we must to avoid them.
 * https://sourceware.org/bugzilla/show_bug.cgi?id=190
 *
 * gULL add ull when __ASSEMBLER__ is not defined
 */
#ifdef __ASSEMBLER__
# define gULL(u)	(u)
#else
# define gULL(u)	(u##ull)
#endif

#define KiB	gULL(1024)
#define MiB	gULL(1048576)
#define GiB	gULL(1073741824)
#define TiB	gULL(1099511627776)
#define PiB	gULL(1125899906842624)
#define EiB	gULL(1152921504606846976)

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
#define ROUNDDN(x, y)	(((x)/(y))*(y))
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define	BI2WD		32			/* bits per word */
#define	BY2WD		4			/* bytes per word */
#define BY2V		8			/* bytes per double word */
#define BY2SE		8			/* bytes per stack element */
#define BLOCKALIGN	8

#define PGSZ		(4*KiB)			/* page size */
#define PGSHFT		12			/* log(PGSZ) */
#define PTSZ		(4*KiB)			/* page table page size */
#define PTSHFT		9			/*  */

#define MACHSZ		(4*KiB)			/* Mach+stack size */
#define MACHMAX		32			/* max. number of cpus */
#define MACHSTKSZ	(6*(4*KiB))		/* Mach stack size */

#define KSTACK		(16*1024)		/* Size of Proc kernel stack */
#define STACKALIGN(sp)	((sp) & ~(BY2SE-1))	/* bug: assure with alloc */

/*
 * Time
 */
#define HZ		(100)			/* clock frequency */
#define MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 *  Address spaces
 *
 *  (Kernel gets loaded at 1*MiB+64*KiB.
 *	mem from 0 to the end of kernel is not used for other things.)
 *
 *  User is at low addresses; kernel vm starts at KZERO
 *	KSEG0 maps the first TMFM bytes, one to one,(i.e KZERO)
 *  KSEG1 maps the PML4 into itself.
 *  KSEG2 maps all remaining physical memory. (from TMFM up).
 */

#define UTZERO		(0+2*MiB)		/* first address in user text */
#define UTROUND(t)	ROUNDUP((t), 2*MiB)	/* first address beyond text for user data */
#define USTKTOP		gULL(0x00007ffffffff000)
#define USTKSIZE	(16*1024*1024)		/* size of user stack */
#define TSTKTOP		(USTKTOP-USTKSIZE)	/* end of new stack in sysexec */

#define KSEG0		gULL(0xfffffffff0000000)	/* 256MB - this is confused */
#define KSEG1		gULL(0xffffff0000000000)	/* 512GB - embedded PML4 */
#define KSEG2		gULL(0xfffffe0000000000)	/* 1TB - KMAP */
#define PMAPADDR	gULL(0xffffffffffe00000)	/* unused as of yet (KMAP?) */

#define KZERO		gULL(0xfffffffff0000000)
#define KTZERO		(KZERO+1*MiB+64*KiB)

/*
 *  virtual MMU
 */
#define	PTEMAPMEM	(1ull*MiB)	
#define	PTEPERTAB	(PTEMAPMEM/PGSZ)
#define	SEGMAPSIZE	65536
#define SSEGMAPSIZE	16
#define SEGMAXPG	(SEGMAPSIZE)

/*
 * This is the interface between fixfault and mmuput.
 * Should be in port.
 */
#define PTEVALID	(1<<0)
#define PTEWRITE	(1<<1)
#define PTERONLY	(0<<1)
#define PTEUSER		(1<<2)
#define PTEUNCACHED	(1<<4)

#define getpgcolor(a)	0

/*
 * Hierarchical Page Tables.
 * For example, traditional IA-32 paging structures have 2 levels,
 * level 1 is the PD, and level 0 the PT pages; with IA-32e paging,
 * level 3 is the PML4(!), level 2 the PDP, level 1 the PD,
 * and level 0 the PT pages. The PTLX macro gives an index into the
 * page-table page at level 'l' for the virtual address 'v'.
 */
#define PTLX(v, l)	(((v)>>(((l)*PTSHFT)+PGSHFT)) & ((1<<PTSHFT)-1))
#define PGLSZ(l)	(1<<(((l)*PTSHFT)+PGSHFT))

#define TMFM		((256-32)*MiB)			/* GAK kernel memory */

/* this can go when the arguments to mmuput change */
#define PPN(x)		((x) & ~(PGSZ-1))		/* GAK */

#define	CACHELINESZ	64

#define	KVATOP	(KSEG0&KSEG1&KSEG2)
#define	iskaddr(a)	(((uintptr_t)(a)&KVATOP) == KVATOP)
