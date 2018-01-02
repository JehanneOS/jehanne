/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

/* override default macros from ../port/usb.h */
#undef	dprint
#undef	ddprint
#undef	deprint
#undef	ddeprint
#define dprint(...)	do if(ehcidebug)jehanne_print(__VA_ARGS__); while(0)
#define ddprint(...)	do if(ehcidebug>1)jehanne_print(__VA_ARGS__); while(0)
#define deprint(...)	do if(ehcidebug || ep->debug)jehanne_print(__VA_ARGS__); while(0)
#define ddeprint(...)	do if(ehcidebug>1 || ep->debug>1)jehanne_print(__VA_ARGS__); while(0)

typedef struct Ctlr Ctlr;
typedef struct Eopio Eopio;
typedef struct Isoio Isoio;
typedef struct Poll Poll;
typedef struct Qh Qh;
typedef struct Qtree Qtree;

#pragma incomplete Ctlr;
#pragma incomplete Ecapio;
#pragma incomplete Edbgio;
#pragma incomplete Eopio;
#pragma incomplete Isoio;
#pragma incomplete Poll;
#pragma incomplete Qh;
#pragma incomplete Qtree;

struct Poll
{
	Lock	l;
	Rendez	r;
	int	must;
	int	does;
};

struct Ctlr
{
	Rendez	r;		/* for waiting to async advance doorbell */
	Lock	l;		/* for ilock. qh lists and basic ctlr I/O */
	QLock	portlck;	/* for port resets/enable... (and doorbell) */
	int	active;		/* in use or not */
	Pcidev*	pcidev;
	Ecapio*	capio;		/* Capability i/o regs */
	Eopio*	opio;		/* Operational i/o regs */

	void*	(*tdalloc)(uint32_t,int,uint32_t);
	void*	(*dmaalloc)(uint32_t);
	void	(*dmafree)(void*);
	void	(*dmaflush)(int,void*,uint32_t len);

	int	nframes;	/* 1024, 512, or 256 frames in the list */
	uint32_t*	frames;		/* periodic frame list (hw) */
	Qh*	qhs;		/* async Qh circular list for bulk/ctl */
	Qtree*	tree;		/* tree of Qhs for the periodic list */
	int	ntree;		/* number of dummy qhs in tree */
	Qh*	intrqhs;		/* list of (not dummy) qhs in tree  */
	Isoio*	iso;		/* list of active Iso I/O */
	uint32_t	load;
	uint32_t	isoload;
	int	nintr;		/* number of interrupts attended */
	int	ntdintr;	/* number of intrs. with something to do */
	int	nqhintr;	/* number of async td intrs. */
	int	nisointr;	/* number of periodic td intrs. */
	int	nreqs;
	Poll	poll;
};

/*
 * PC-specific stuff
 */

/*
 * Operational registers (hw)
 */
struct Eopio
{
	uint32_t	cmd;		/* 00 command */
	uint32_t	sts;		/* 04 status */
	uint32_t	intr;		/* 08 interrupt enable */
	uint32_t	frno;		/* 0c frame index */
	uint32_t	seg;		/* 10 bits 63:32 of EHCI datastructs (unused) */
	uint32_t	frbase;		/* 14 frame list base addr, 4096-byte boundary */
	uint32_t	link;		/* 18 link for async list */
	uint8_t		d2c[0x40-0x1c];	/* 1c dummy */
	uint32_t	config;		/* 40 1: all ports default-routed to this HC */
	uint32_t	portsc[1];	/* 44 Port status and control, one per port */
};

extern int ehcidebug;
extern Ecapio *ehcidebugcapio;
extern int ehcidebugport;

void	ehcilinkage(Hci *hp);
void	ehcimeminit(Ctlr *ctlr);
void	ehcirun(Ctlr *ctlr, int on);
