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
#define	ROUND(s, sz)	(((s)+((sz)-1))&~((sz)-1))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
#define ROUNDDN(x, y)	(((x)/(y))*(y))
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define	BI2WD		32			/* bits per word */
#define	BY2WD		8			/* bytes per word */
#define BY2V		8			/* bytes per double word */
#define BY2SE		8			/* bytes per stack element */
#define BLOCKALIGN	8

#define PGSZ		(4*KiB)			/* page size */
#define	BY2PG		PGSZ
#define	WD2PG		(BY2PG/BY2WD)
#define PGSHFT		12			/* log(PGSZ) */
#define PGSHIFT		PGSHFT
#define	PGROUND(s)	ROUNDUP(s, PGSZ)
#define	BLOCKALIGN	8
#define	FPalign		16

#define MACHSZ		(2*PGSZ)		/* Mach+stack size */
#define MACHMAX		32			/* max. number of cpus */
#define MACHSTKSZ	(6*(4*KiB))		/* Mach stack size */

#define KSTACK		(16*KiB)		/* Size of Proc kernel stack */
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
#define UADDRMASK	gULL(0x00007fffffffffff)		/* canonical address mask */
#define TSTKTOP		(0x00007ffffffff000ull)
#define USTKSIZE	(16*MiB)			/* size of user stack */
#define USTKTOP		(TSTKTOP-USTKSIZE)		/* end of new stack in sysexec */

//#define KSEG0		gULL(0xffffffff80000000)	/* 256MB - this is confused */
//#define KSEG1		gULL(0xffffff0000000000)	/* 512GB - embedded PML4 */
//#define KSEG2		gULL(0xfffffe8000000000)	/* 1TB - KMAP */
#define PMAPADDR	gULL(0xffffffffffe00000)	/* unused as of yet (KMAP?) */


#define KZERO		gULL(0xffffffff80000000)
#define KTZERO		(KZERO+1*MiB+64*KiB)

#define VMAP		gULL(0xffffff0000000000)
#define VMAPSIZE	(512*GiB)

#define	KMAP		gULL(0xfffffe8000000000)
#define KMAPSIZE	(2*MiB)

/*
 * Fundamental addresses - bottom 64kB saved for return to real mode
 */
#define	CONFADDR	(KZERO+0x1200)		/* info passed from boot loader */
#define	APBOOTSTRAP	(KZERO+0x3000)		/* AP bootstrap code */
#define	IDTADDR		(KZERO+0x10000)		/* idt */
#define	REBOOTADDR	(0x11000)			/* reboot code - physical address */

#define CPU0PML4	(KZERO+0x13000)
#define CPU0PDP		(KZERO+0x14000)
#define CPU0PD0		(KZERO+0x15000)		/* KZERO */
#define CPU0PD1		(KZERO+0x16000)		/* KZERO+1GB */

#define	CPU0GDT		(KZERO+0x17000)		/* bootstrap processor GDT */

#define	CPU0MACH	(KZERO+0x18000)		/* Mach for bootstrap processor */
#define CPU0END		(CPU0MACH+MACHSIZE)

#define	MACHSIZE	(2*KSTACK)

/*
 *  virtual MMU
 */
#define	PTEMAPMEM	(1ull*MiB)	
#define	PTEPERTAB	(PTEMAPMEM/PGSZ)
#define	SEGMAPSIZE	65536
#define SSEGMAPSIZE	16
#define SEGMAXPG	(SEGMAPSIZE)

/*
 *  physical MMU
 */
#define	PTEVALID	(gULL(1)<<0)
#define	PTEWT		(gULL(1)<<3)
#define	PTEUNCACHED	(gULL(1)<<4)
#define	PTEWRITE	(gULL(1)<<1)
#define	PTERONLY	(gULL(0)<<1)
#define	PTEKERNEL	(gULL(0)<<2)
#define	PTEUSER		(gULL(1)<<2)
#define	PTESIZE		(gULL(1)<<7)
#define	PTEGLOBAL	(gULL(1)<<8)

#define getpgcolor(a)	0

/*
 * Hierarchical Page Tables.
 * For example, traditional IA-32 paging structures have 2 levels,
 * level 1 is the PD, and level 0 the PT pages; with IA-32e paging,
 * level 3 is the PML4(!), level 2 the PDP, level 1 the PD,
 * and level 0 the PT pages. The PTLX macro gives an index into the
 * page-table page at level 'l' for the virtual address 'v'.
 */
#define PTSZ		(4*KiB)			/* page table page size */
#define PTSHFT		9			/*  */

#define PTLX(v, l)	(((v)>>(((l)*PTSHFT)+PGSHFT)) & ((1<<PTSHFT)-1))
#define PGLSZ(l)	(1<<(((l)*PTSHFT)+PGSHFT))

#define TMFM		((256-32)*MiB)			/* GAK kernel memory */

/* this can go when the arguments to mmuput change */
#define PPN(x)		((x) & ~(PGSZ-1))		/* GAK */

#define	CACHELINESZ	64

//#define	KVATOP	(KSEG0&KSEG1&KSEG2)
#define	iskaddr(a)	(((uintptr_t)(a)) > KZERO)

/*
 *  known x86 segments (in GDT) and their selectors
 */
#define	NULLSEG	0	/* null segment */
#define	KESEG	1	/* kernel executable */
#define	KDSEG	2	/* kernel data */
#define	UE32SEG	3	/* user executable 32bit */
#define	UDSEG	4	/* user data/stack */
#define	UESEG	5	/* user executable 64bit */
#define	TSSSEG	8	/* task segment (two descriptors) */

#define	NGDT	10	/* number of GDT entries required */

#define	SELGDT	(0<<2)	/* selector is in gdt */
#define	SELLDT	(1<<2)	/* selector is in ldt */

#define	SELECTOR(i, t, p)	(((i)<<3) | (t) | (p))

#define	NULLSEL	SELECTOR(NULLSEG, SELGDT, 0)
#define	KESEL	SELECTOR(KESEG, SELGDT, 0)
#define	UE32SEL	SELECTOR(UE32SEG, SELGDT, 3)
#define	UDSEL	SELECTOR(UDSEG, SELGDT, 3)
#define	UESEL	SELECTOR(UESEG, SELGDT, 3)
#define	TSSSEL	SELECTOR(TSSSEG, SELGDT, 0)

/*
 *  fields in segment descriptors
 */
#define	SEGDATA	(0x10<<8)	/* data/stack segment */
#define	SEGEXEC	(0x18<<8)	/* executable segment */
#define	SEGTSS	(0x9<<8)	/* TSS segment */
#define	SEGCG	(0x0C<<8)	/* call gate */
#define	SEGIG	(0x0E<<8)	/* interrupt gate */
#define	SEGTG	(0x0F<<8)	/* trap gate */
#define	SEGLDT	(0x02<<8)	/* local descriptor table */
#define	SEGTYPE	(0x1F<<8)

#define	SEGP	(1<<15)		/* segment present */
#define	SEGPL(x) ((x)<<13)	/* priority level */
#define	SEGB	(1<<22)		/* granularity 1==4k (for expand-down) */
#define	SEGD	(1<<22)		/* default 1==32bit (for code) */
#define	SEGE	(1<<10)		/* expand down */
#define	SEGW	(1<<9)		/* writable (for data/stack) */
#define	SEGR	(1<<9)		/* readable (for code) */
#define SEGL	(1<<21)		/* 64 bit */
#define	SEGG	(1<<23)		/* granularity 1==4k (for other) */

#define	getpgcolor(a)	0
#define MBPGMASK (~(PGSZ-1))
