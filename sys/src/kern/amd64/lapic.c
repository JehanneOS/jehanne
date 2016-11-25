#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "apic.h"
#include "io.h"
#include "adr.h"

#undef DBG
#define	DBG	print

enum {						/* Local APIC registers */
	Id		= 0x0020,		/* Identification */
	Ver		= 0x0030,		/* Version */
	Tp		= 0x0080,		/* Task Priority */
	Ap		= 0x0090,		/* Arbitration Priority */
	Pp		= 0x00a0,		/* Processor Priority */
	Eoi		= 0x00b0,		/* EOI */
	Ld		= 0x00d0,		/* Logical Destination */
	Df		= 0x00e0,		/* Destination Format */
	Siv		= 0x00f0,		/* Spurious Interrupt Vector */
	Is		= 0x0100,		/* Interrupt Status (8) */
	Tm		= 0x0180,		/* Trigger Mode (8) */
	Ir		= 0x0200,		/* Interrupt Request (8) */
	Es		= 0x0280,		/* Error Status */
	Iclo		= 0x0300,		/* Interrupt Command */
	Ichi		= 0x0310,		/* Interrupt Command [63:32] */
	Lvt0		= 0x0320,		/* Local Vector Table 0 */
	Lvt5		= 0x0330,		/* Local Vector Table 5 */
	Lvt4		= 0x0340,		/* Local Vector Table 4 */
	Lvt1		= 0x0350,		/* Local Vector Table 1 */
	Lvt2		= 0x0360,		/* Local Vector Table 2 */
	Lvt3		= 0x0370,		/* Local Vector Table 3 */
	Tic		= 0x0380,		/* Timer Initial Count */
	Tcc		= 0x0390,		/* Timer Current Count */
	Tdc		= 0x03e0,		/* Timer Divide Configuration */

	Tlvt		= Lvt0,			/* Timer */
	Lint0		= Lvt1,			/* Local Interrupt 0 */
	Lint1		= Lvt2,			/* Local Interrupt 1 */
	Elvt		= Lvt3,			/* Error */
	Pclvt		= Lvt4,			/* Performance Counter */
	Tslvt		= Lvt5,			/* Thermal Sensor */
};

enum {						/* Siv */
	Swen		= 0x00000100,		/* Software Enable */
	Fdis		= 0x00000200,		/* Focus Disable */
};

enum {						/* Iclo */
	Lassert		= 0x00004000,		/* Assert level */

	DSnone		= 0x00000000,		/* Use Destination Field */
	DSself		= 0x00040000,		/* Self is only destination */
	DSallinc	= 0x00080000,		/* All including self */
	DSallexc	= 0x000c0000,		/* All Excluding self */
};

enum {						/* Tlvt */
	Periodic	= 0x00020000,		/* Periodic Timer Mode */
};

enum {						/* Tdc */
	DivX2		= 0x00000000,		/* Divide by 2 */
	DivX4		= 0x00000001,		/* Divide by 4 */
	DivX8		= 0x00000002,		/* Divide by 8 */
	DivX16		= 0x00000003,		/* Divide by 16 */
	DivX32		= 0x00000008,		/* Divide by 32 */
	DivX64		= 0x00000009,		/* Divide by 64 */
	DivX128		= 0x0000000a,		/* Divide by 128 */
	DivX1		= 0x0000000b,		/* Divide by 1 */
};

static uint32_t* lapicbase;

static	Lapic	xlapic[Napic];

Lapic*
lapiclookup(uint32_t id)
{
	Lapic *a;

	if(id >= nelem(xlapic))
		return nil;
	a = xlapic + id;
	if(a->useable)
		return a;
	return nil;
}

static uint32_t
lapicrget(int r)
{
	return lapicbase[r/4];
}

static void
lapicrput(int r, uint32_t data)
{
	if(lapicbase != nil){
		/* early panics can occur with lapicbase uninitialized
		 * but if we let it fault, we loose the actual panic PC
		 */
		lapicbase[r/4] = data;
	}
}

int
lapiceoi(int vecno)
{
	lapicrput(Eoi, 0);
	return vecno;
}

int
lapicisr(int vecno)
{
	int isr;

	isr = lapicrget(Is + (vecno/32)*16);

	return isr & (1<<(vecno%32));
}

static char*
lapicprint(char *p, char *e, Lapic *a, int i)
{
	char *s;

	s = "proc";
	p = seprint(p, e, "%-8s ", s);
	p = seprint(p, e, "%8ux ", i);
//	p = seprint(p, e, "%.8ux ", a->dest);
//	p = seprint(p, e, "%.8ux ", a->mask);
//	p = seprint(p, e, "%c", a->flags & PcmpBP? 'b': ' ');
//	p = seprint(p, e, "%c ", a->flags & PcmpEN? 'e': ' ');
//	p = seprint(p, e, "%8ux %8ux", a->lintr[0], a->lintr[1]);
	p = seprint(p, e, "%12d\n", a->machno);
	return p;
}

static long
lapicread(Chan* _1, void *a, long n, int64_t off)
{
	char *s, *e, *p;
	long i, r;

	s = malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < nelem(xlapic); i++)
		if(xlapic[i].useable)
			p = lapicprint(p, e, xlapic + i, i);
	r = -1;
	if(!waserror()){
		r = readstr(off, a, n, s);
		poperror();
	}
	free(s);
	return r;
}

void
lapicinit(int lapicno, uintmem pa, int isbp)
{
	Lapic *apic;

	/*
	 * Mark the LAPIC useable if it has a good ID, and the registers can
	 * be mapped.  There is x2LAPIC to be dealt with at some point.
	 */
	DBG("lapicinit: lapicno %d pa %#P isbp %d caller %#p\n", lapicno, pa, isbp, getcallerpc());
	addarchfile("lapic", 0444, lapicread, nil);

	if(lapicno >= Napic){
		panic("lapicinit%d: out of range", lapicno);
		return;
	}
	if((apic = &xlapic[lapicno])->useable){
		print("lapicinit%d: already initialised\n", lapicno);
		return;
	}
	if(lapicbase == nil){
		//adrmapck(pa, 1024, Ammio, Mfree, Cnone);
		if((lapicbase = vmap(pa, 1024)) == nil){
			panic("lapicinit%d: can't map lapicbase %#P", lapicno, pa);
			return;
		}
		DBG("lapicinit%d: lapicbase %#P -> %#p\n", lapicno, pa, lapicbase);
	}
	apic->useable = 1;

	/*
	 * Assign a machno to the processor associated with this
	 * LAPIC, it may not be an identity map.
	 * Machno 0 is always the bootstrap processor.
	 */
	if(isbp){
		apic->machno = 0;
		m->apicno = lapicno;
	}
	else
		apic->machno = sys->nmach++;
}

void
lapicsetdom(int lapicno, int dom)
{
	Lapic *apic;

	DBG("lapic%d: setdom: %d\n", lapicno, dom);
	if(lapicno >= Napic){
		panic("lapic%d: lapicsetdom: apic out of range", lapicno);
		return;
	}
	if((apic = &xlapic[lapicno])->useable)
		apic->dom = dom;
	else
		print("lapic%d: lapicsetdom: apic not usable\n", lapicno);
}

int
machdom(Mach *mp)
{
	return xlapic[mp->apicno].dom;
}

static void
lapicdump0(Lapic *apic, int i)
{
	if(!apic->useable)
		return;
	DBG("lapic%d: machno %d lint0 %#8.8ux lint1 %#8.8ux\n",
		i, apic->machno, apic->lvt[0], apic->lvt[1]);
	DBG(" tslvt %#8.8ux pclvt %#8.8ux elvt %#8.8ux\n",
		lapicrget(Tslvt), lapicrget(Pclvt), lapicrget(Elvt));
	DBG(" tlvt %#8.8ux lint0 %#8.8ux lint1 %#8.8ux siv %#8.8ux\n",
		lapicrget(Tlvt), lapicrget(Lint0),
		lapicrget(Lint1), lapicrget(Siv));
}

void
lapicdump(void)
{
	int i;

	if(!DBGFLG)
		return;

	DBG("lapicbase %#p\n", lapicbase);
	for(i = 0; i < Napic; i++)
		lapicdump0(xlapic + i, i);
}

int
lapiconline(void)
{
	Lapic *apic;
	uint64_t tsc;
	uint32_t dfr, ver;
	int apicno, nlvt;

	if(lapicbase == nil)
		panic("lapiconline: no lapic base");

	if((apicno = ((lapicrget(Id)>>24) & 0xff)) >= Napic)
		panic("lapic: id too large %d", apicno);
	if(apicno != m->apicno){
		panic("lapic: %d != %d", m->apicno, apicno);
		dfr = lapicrget(Id) & ~(0xff<<24);
		dfr |= m->apicno<<24;
		lapicrput(Id, dfr);
		apicno = m->apicno;
	}
	apic = &xlapic[apicno];
	if(!apic->useable)
		panic("lapiconline: lapic%d: unusable %d", apicno, apic->useable);

	/*
	 * Things that can only be done when on the processor
	 * owning the APIC, apicinit above runs on the bootstrap
	 * processor.
	 */
	ver = lapicrget(Ver);
	nlvt = ((ver>>16) & 0xff) + 1;
	if(nlvt > nelem(apic->lvt)){
		print("lapiconline%d: nlvt %d > max (%d)\n",
			apicno, nlvt, nelem(apic->lvt));
		nlvt = nelem(apic->lvt);
	}
	apic->nlvt = nlvt;
	apic->ver = ver & 0xff;

	/*
	 * These don't really matter in Physical mode;
	 * set the defaults anyway.
	 */
//	if(memcmp(m->cpuinfo, "AuthenticAMD", 12) == 0)
//		dfr = 0xf0000000;
//	else
		dfr = 0xffffffff;
	lapicrput(Df, dfr);
	lapicrput(Ld, 0x00000000);

	/*
	 * Disable interrupts until ready by setting the Task Priority
	 * register to 0xff.
	 */
	lapicrput(Tp, 0xff);

	/*
	 * Software-enable the APIC in the Spurious Interrupt Vector
	 * register and set the vector number. The vector number must have
	 * bits 3-0 0x0f unless the Extended Spurious Vector Enable bit
	 * is set in the HyperTransport Transaction Control register.
	 */
	lapicrput(Siv, Swen|IdtSPURIOUS);

	/*
	 * Acknowledge any outstanding interrupts.
	 */
	lapicrput(Eoi, 0);

	/*
	 * Use the TSC to determine the lapic timer frequency.
	 * It might be possible to snarf this from a chipset
	 * register instead.
	 */
	lapicrput(Tdc, DivX1);
	lapicrput(Tlvt, Im);
	tsc = rdtsc() + m->cpuhz/10;
	lapicrput(Tic, 0xffffffff);

	while(rdtsc() < tsc)
		;

	apic->hz = (0xffffffff-lapicrget(Tcc))*10;
	apic->max = apic->hz/HZ;
	apic->min = apic->hz/(100*HZ);
	apic->div = ((m->cpuhz/apic->max)+HZ/2)/HZ;

	if(m->machno == 0 || DBGFLG){
		print("lapic%d: hz %lld max %lld min %lld div %lld\n", apicno,
			apic->hz, apic->max, apic->min, apic->div);
	}

	/*
	 * Mask interrupts on Performance Counter overflow and
	 * Thermal Sensor if implemented, and on Lintr0 (Legacy INTR),
	 * and Lintr1 (Legacy NMI).
	 * Clear any Error Status (write followed by read) and enable
	 * the Error interrupt.
	 */
	switch(apic->nlvt){
	case 7:
	case 6:
		lapicrput(Tslvt, Im);
		/*FALLTHROUGH*/
	case 5:
		lapicrput(Pclvt, Im);
		/*FALLTHROUGH*/
	default:
		break;
	}
	lapicrput(Lint1, apic->lvt[1]|Im|IdtLINT1);
	lapicrput(Lint0, apic->lvt[0]|Im|IdtLINT0);

	lapicrput(Es, 0);
	lapicrget(Es);
	lapicrput(Elvt, IdtERROR);

	/*
	 * Reload the timer to de-synchronise the processors.
	 * When the caller is ready for the APIC to accept interrupts,
	 * it should call lapicpri to lower the task priority.
	 *
	 * The timer is enabled later by the core-specific startup
	 * i.e. don't start the timer unless the core needs it,
	 * to reduce the likelihood of at least one (spurious) interrupt
	 * from the timer when priority is lowered.
	 */
	microdelay((TK2MS(1)*1000/sys->nmach) * m->machno);
	lapicrput(Tic, apic->max);
	return 1;
}

void
lapictimerenable(void)
{
	/*
	 * Perhaps apictimerenable/apictimerdisable should just
	 * clear/set Im in the existing settings of Tlvt, there may
	 * be a time when the timer is used in a different mode;
	 * if so will need to ensure the mode is set when the timer
	 * is initialised.
	 */
	lapicrput(Tlvt, Periodic|IdtTIMER);
}

void
lapictimerdisable(void)
{
	lapicrput(Tlvt, Im|IdtTIMER);
}

void
lapictimerset(uint64_t next)
{
	Lapic *apic;
	int64_t period;

	apic = &xlapic[(lapicrget(Id)>>24) & 0xff];

	ilock(&m->apictimerlock);

	period = apic->max;
	if(next != 0){
		period = next - fastticks(nil);	/* fastticks is just rdtsc() */
		period /= apic->div;

		if(period < apic->min)
			period = apic->min;
		else if(period > apic->max - apic->min)
			period = apic->max;
	}
	lapicrput(Tic, period);

	iunlock(&m->apictimerlock);
}

void
lapicsipi(int lapicno, uintmem pa)
{
	int i;
	uint32_t crhi, crlo;

	/*
	 * SIPI - Start-up IPI.
	 * To do: checks on lapic validity.
	 */
	crhi = lapicno<<24;
	lapicrput(Ichi, crhi);
	lapicrput(Iclo, DSnone|TMlevel|Lassert|MTir);
	microdelay(200);
	lapicrput(Iclo, DSnone|TMlevel|MTir);
	delay(10);

	crlo = DSnone|TMedge|MTsipi|((uint32_t)pa/(4*KiB));
	for(i = 0; i < 2; i++){
		lapicrput(Ichi, crhi);
		lapicrput(Iclo, crlo);
		microdelay(200);
	}
}

void
lapicipi(int lapicno)
{
	lapicrput(Ichi, lapicno<<24);
	lapicrput(Iclo, DSnone|TMedge|Lassert|MTf|IdtIPI);
	while(lapicrget(Iclo) & Ds)
		;
}

void
lapicpri(int pri)
{
	lapicrput(Tp, pri);
}
