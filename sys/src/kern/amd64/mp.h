/*
 * MultiProcessor Specification Version 1.[14].
 */
typedef struct {			/* floating pointer */
	uint8_t	signature[4];		/* "_MP_" */
	int32_t	physaddr;		/* physical address of MP configuration table */
	uint8_t	length;			/* 1 */
	uint8_t	specrev;		/* [14] */
	uint8_t	checksum;		/* all bytes must add up to 0 */
	uint8_t	type;			/* MP system configuration type */
	uint8_t	imcrp;
	uint8_t	reserved[3];
} _MP_;

#define _MP_sz			(4+4+1+1+1+1+1+3)

typedef struct {			/* configuration table header */
	uint8_t		signature[4];		/* "PCMP" */
	uint16_t	length;			/* total table length */
	uint8_t		version;		/* [14] */
	uint8_t		checksum;		/* all bytes must add up to 0 */
	uint8_t		product[20];		/* product id */
	uint32_t	oemtable;		/* OEM table pointer */
	uint16_t	oemlength;		/* OEM table length */
	uint16_t	entry;			/* entry count */
	uint32_t	lapicbase;		/* address of local APIC */
	uint16_t	xlength;		/* extended table length */
	uint8_t		xchecksum;		/* extended table checksum */
	uint8_t		reserved;
} PCMP;

#define PCMPsz			(4+2+1+1+20+4+2+2+4+2+1+1)

typedef struct {			/* processor table entry */
	uint8_t		type;			/* entry type (0) */
	uint8_t		apicno;			/* local APIC id */
	uint8_t		version;		/* local APIC verison */
	uint8_t		flags;			/* CPU flags */
	uint8_t		signature[4];		/* CPU signature */
	uint32_t	feature;		/* feature flags from CPUID instruction */
	uint8_t		reserved[8];
} PCMPprocessor;

#define PCMPprocessorsz		(1+1+1+1+4+4+8)

typedef struct {			/* bus table entry */
	uint8_t	type;			/* entry type (1) */
	uint8_t	busno;			/* bus id */
	char	string[6];		/* bus type string */
} PCMPbus;

#define PCMPbussz		(1+1+6)

typedef struct {			/* I/O APIC table entry */
	uint8_t	type;			/* entry type (2) */
	uint8_t	apicno;			/* I/O APIC id */
	uint8_t	version;		/* I/O APIC version */
	uint8_t	flags;			/* I/O APIC flags */
	uint32_t	addr;			/* I/O APIC address */
} PCMPioapic;

#define PCMPioapicsz		(1+1+1+1+4)

typedef struct {			/* interrupt table entry */
	uint8_t		type;			/* entry type ([34]) */
	uint8_t		intr;			/* interrupt type */
	uint16_t	flags;			/* interrupt flag */
	uint8_t		busno;			/* source bus id */
	uint8_t		irq;			/* source bus irq */
	uint8_t		apicno;			/* destination APIC id */
	uint8_t		intin;			/* destination APIC [L]INTIN# */
} PCMPintr;

#define PCMPintrsz		(1+1+2+1+1+1+1)

typedef struct {			/* system address space mapping entry */
	uint8_t		type;			/* entry type (128) */
	uint8_t		length;			/* of this entry (20) */
	uint8_t		busno;			/* bus id */
	uint8_t		addrtype;
	uint32_t	addrbase[2];
	uint32_t	addrlength[2];
} PCMPsasm;

#define PCMPsasmsz		(1+1+1+1+8+8)

typedef struct {			/* bus hierarchy descriptor entry */
	uint8_t	type;			/* entry type (129) */
	uint8_t	length;			/* of this entry (8) */
	uint8_t	busno;			/* bus id */
	uint8_t	info;			/* bus info */
	uint8_t	parent;			/* parent bus */
	uint8_t	reserved[3];
} PCMPhierarchy;

#define PCMPhirarchysz		(1+1+1+1+1+3)

typedef struct {			/* compatibility bus address space modifier entry */
	uint8_t		type;			/* entry type (130) */
	uint8_t		length;			/* of this entry (8) */
	uint8_t		busno;			/* bus id */
	uint8_t		modifier;		/* address modifier */
	uint32_t	range;			/* predefined range list */
} PCMPcbasm;

#define PCMPcbasmsz		(1+1+1+1+4)

enum {					/* table entry types */
	PcmpPROCESSOR	= 0x00,		/* one entry per processor */
	PcmpBUS		= 0x01,		/* one entry per bus */
	PcmpIOAPIC	= 0x02,		/* one entry per I/O APIC */
	PcmpIOINTR	= 0x03,		/* one entry per bus interrupt source */
	PcmpLINTR	= 0x04,		/* one entry per system interrupt source */

	PcmpSASM	= 0x80,
	PcmpHIERARCHY	= 0x81,
	PcmpCBASM	= 0x82,

					/* PCMPprocessor and PCMPioapic flags */
	PcmpEN		= 0x01,		/* enabled */
	PcmpBP		= 0x02,		/* bootstrap processor */

					/* PCMPiointr and PCMPlintr flags */
	PcmpPOMASK	= 0x03,		/* polarity conforms to specifications of bus */
	PcmpHIGH	= 0x01,		/* active high */
	PcmpLOW		= 0x03,		/* active low */
	PcmpELMASK	= 0x0C,		/* trigger mode of APIC input signals */
	PcmpEDGE	= 0x04,		/* edge-triggered */
	PcmpLEVEL	= 0x0C,		/* level-triggered */

					/* PCMPiointr and PCMPlintr interrupt type */
	PcmpINT		= 0x00,		/* vectored interrupt from APIC Rdt */
	PcmpNMI		= 0x01,		/* non-maskable interrupt */
	PcmpSMI		= 0x02,		/* system management interrupt */
	PcmpExtINT	= 0x03,		/* vectored interrupt from external PIC */

					/* PCMPsasm addrtype */
	PcmpIOADDR	= 0x00,		/* I/O address */
	PcmpMADDR	= 0x01,		/* memory address */
	PcmpPADDR	= 0x02,		/* prefetch address */

					/* PCMPhierarchy info */
	PcmpSD		= 0x01,		/* subtractive decode bus */

					/* PCMPcbasm modifier */
	PcmpPR		= 0x01,		/* predefined range list */
};

/*
 * Condensed form of the MP Configuration Table.
 * This is created during a single pass through the MP Configuration
 * table.
 */
typedef struct Aintr Aintr;
typedef struct Bus Bus;
typedef struct Apic Apic;

typedef struct Bus {
	uint8_t	type;
	uint8_t	busno;
	uint8_t	po;
	uint8_t	el;

	Aintr*	aintr;			/* interrupts tied to this bus */
	Bus*	next;
} Bus;

typedef struct Aintr {
	PCMPintr* intr;
	Apic*	apic;
	Aintr*	next;
} Aintr;

typedef struct Apic {
	int		type;
	int		apicno;
	uint32_t*	addr;			/* register base address */
	uint32_t	paddr;
	int		flags;			/* PcmpBP|PcmpEN */

	Lock;					/* I/O APIC: register access */
	int		mre;			/* I/O APIC: maximum redirection entry */
	int		gsibase;		/* I/O APIC: global system interrupt base (acpi) */

	int		lintr[2];		/* Local APIC */
	int		machno;

	int		online;
} Apic;

enum {
	MaxAPICNO	= 254,		/* 255 is physical broadcast */
};

enum {					/* I/O APIC registers */
	IoapicID	= 0x00,		/* ID */
	IoapicVER	= 0x01,		/* version */
	IoapicARB	= 0x02,		/* arbitration ID */
	IoapicRDT	= 0x10,		/* redirection table */
};

/*
 * Common bits for
 *	I/O APIC Redirection Table Entry;
 *	Local APIC Local Interrupt Vector Table;
 *	Local APIC Inter-Processor Interrupt;
 *	Local APIC Timer Vector Table.
 */
enum {
	ApicFIXED	= 0x00000000,	/* [10:8] Delivery Mode */
	ApicLOWEST	= 0x00000100,	/* Lowest priority */
	ApicSMI		= 0x00000200,	/* System Management Interrupt */
	ApicRR		= 0x00000300,	/* Remote Read */
	ApicNMI		= 0x00000400,
	ApicINIT	= 0x00000500,	/* INIT/RESET */
	ApicSTARTUP	= 0x00000600,	/* Startup IPI */
	ApicExtINT	= 0x00000700,

	ApicPHYSICAL	= 0x00000000,	/* [11] Destination Mode (RW) */
	ApicLOGICAL	= 0x00000800,

	ApicDELIVS	= 0x00001000,	/* [12] Delivery Status (RO) */
	ApicHIGH	= 0x00000000,	/* [13] Interrupt Input Pin Polarity (RW) */
	ApicLOW		= 0x00002000,
	ApicRemoteIRR	= 0x00004000,	/* [14] Remote IRR (RO) */
	ApicEDGE	= 0x00000000,	/* [15] Trigger Mode (RW) */
	ApicLEVEL	= 0x00008000,
	ApicIMASK	= 0x00010000,	/* [16] Interrupt Mask */
};

extern void ioapicinit(Apic*, int);
extern void ioapicrdtr(Apic*, int, int*, int*);
extern void ioapicrdtw(Apic*, int, int, int);

extern void lapicclock(Ureg*, void*);
extern int lapiceoi(int);
extern void lapicerror(Ureg*, void*);
extern void lapicicrw(uint32_t, uint32_t);
extern void lapicinit(Apic*);
extern void lapicintroff(void);
extern void lapicintron(void);
extern int lapicisr(int);
extern void lapicnmidisable(void);
extern void lapicnmienable(void);
extern void lapiconline(void);
extern void lapicspurious(Ureg*, void*);
extern void lapicstartap(Apic*, uintptr_t);
extern void lapictimerset(uint64_t);

extern int mpintrinit(Bus*, PCMPintr*, int, int);
extern void mpinit(void);
extern int mpintrenable(Vctl*);
extern void mpshutdown(void);
extern void mpstartap(Apic*);

extern Bus* mpbus;
extern Bus* mpbuslast;
extern int mpisabus;
extern int mpeisabus;
extern Apic *mpioapic[];
extern Apic *mpapic[];
