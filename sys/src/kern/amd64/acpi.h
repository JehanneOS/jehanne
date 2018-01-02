/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
typedef	struct	Fadt	Fadt;
typedef	struct	Gas	Gas;
typedef	struct	Tbl	Tbl;

typedef	struct	Acpicfg	Acpicfg;

/*
 * Header for ACPI description tables
 */
struct Tbl {
	uint8_t	sig[4];			/* e.g. "FACP" */
	uint8_t	len[4];
	uint8_t	rev;
	uint8_t	csum;
	uint8_t	oemid[6];
	uint8_t	oemtblid[8];
	uint8_t	oemrev[4];
	uint8_t	creatorid[4];
	uint8_t	creatorrev[4];
	uint8_t	data[];
};

/*
 * Generic address structure. 
 */
struct Gas
{
	uint8_t	spc;	/* address space id */
	uint8_t	len;	/* register size in bits */
	uint8_t	off;	/* bit offset */
	uint8_t	accsz;	/* 1: byte; 2: word; 3: dword; 4: qword */
	uint64_t	addr;	/* address (or acpi encoded tbdf + reg) */
};

/*
 * Fixed ACPI description table.
 */
struct Fadt {
	int	rev;

	uint32_t	facs;
	uint32_t	dsdt;
	uint8_t	pmprofile;
	uint16_t	sciint;
	uint32_t	smicmd;
	uint8_t	acpienable;
	uint8_t	acpidisable;
	uint8_t	s4biosreq;
	uint8_t	pstatecnt;
	uint32_t	pm1aevtblk;
	uint32_t	pm1bevtblk;
	uint32_t	pm1acntblk;
	uint32_t	pm1bcntblk;
	uint32_t	pm2cntblk;
	uint32_t	pmtmrblk;
	uint32_t	gpe0blk;
	uint32_t	gpe1blk;
	uint8_t	pm1evtlen;
	uint8_t	pm1cntlen;
	uint8_t	pm2cntlen;
	uint8_t	pmtmrlen;
	uint8_t	gpe0blklen;
	uint8_t	gpe1blklen;
	uint8_t	gp1base;
	uint8_t	cstcnt;
	uint16_t	plvl2lat;
	uint16_t	plvl3lat;
	uint16_t	flushsz;
	uint16_t	flushstride;
	uint8_t	dutyoff;
	uint8_t	dutywidth;
	uint8_t	dayalrm;
	uint8_t	monalrm;
	uint8_t	century;
	uint16_t	iapcbootarch;
	uint32_t	flags;
	Gas	resetreg;
	uint8_t	resetval;
	uint64_t	xfacs;
	uint64_t	xdsdt;
	Gas	xpm1aevtblk;
	Gas	xpm1bevtblk;
	Gas	xpm1acntblk;
	Gas	xpm1bcntblk;
	Gas	xpm2cntblk;
	Gas	xpmtmrblk;
	Gas	xgpe0blk;
	Gas	xgpe1blk;
};
#pragma	varargck	type	"G"	Gas*

struct Acpicfg {
	uint32_t	sval[6][2];		/* p1a.ctl, p1b.ctl */
};

Tbl*	acpigettbl(void*);

extern	Fadt	fadt;
extern	Acpicfg	acpicfg;

extern void hpetinit(uint32_t, uint32_t, uintmem, int);

enum{
	MemHotPlug=	1<<1,
	MemNonVolatile=	1<<2,
};
extern void memaffinity(uint64_t, uint64_t, uint32_t, int);

/*
 * ACPI 4.0 E820 AddressRange types (table 14-1)
 */
enum {
	AddrsNone		= 0,
	AddrsMemory	= 1,
	AddrsReserved	= 2,
	AddrsACPI	= 3,
	AddrsNVS	= 4,
	AddrsUnusable = 5,
	AddrsDisabled = 6,

	AddrsDEV		= 9,	/* our internal code */

	AddrsNVDIMM	= 0x5a,	/* Viking NVDIMM */

	/* extended attribute flags, not currently used */
	AddrsNonVolatile = 1<<1,
	AddrsSlowAccess = 1<<2,
	AddrsErrorLog = 1<<3,
};
