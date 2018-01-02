/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "mp.h"

static PCMP *pcmp;

static char* buses[] = {
	"CBUSI ",
	"CBUSII",
	"EISA  ",
	"FUTURE",
	"INTERN",
	"ISA   ",
	"MBI   ",
	"MBII  ",
	"MCA   ",
	"MPI   ",
	"MPSA  ",
	"NUBUS ",
	"PCI   ",
	"PCMCIA",
	"TC    ",
	"VL    ",
	"VME   ",
	"XPRESS",
	0,
};

static Bus*
mpgetbus(int busno)
{
	Bus *bus;

	for(bus = mpbus; bus; bus = bus->next)
		if(bus->busno == busno)
			return bus;

	print("mpgetbus: can't find bus %d\n", busno);
	return 0;
}

static Apic*
mkprocessor(PCMPprocessor* p)
{
	static int machno = 1;
	int apicno;
	Apic *apic;

	apicno = p->apicno;
	if(!(p->flags & PcmpEN) || apicno > MaxAPICNO || mpapic[apicno] != nil)
		return 0;

	if((apic = xalloc(sizeof(Apic))) == nil)
		panic("mkprocessor: no memory for Apic");
	apic->type = PcmpPROCESSOR;
	apic->apicno = apicno;
	apic->flags = p->flags;
	apic->lintr[0] = ApicIMASK;
	apic->lintr[1] = ApicIMASK;
	if(p->flags & PcmpBP)
		apic->machno = 0;
	else
		apic->machno = machno++;
	mpapic[apicno] = apic;

	return apic;
}

static Bus*
mkbus(PCMPbus* p)
{
	Bus *bus;
	int i;

	for(i = 0; buses[i]; i++)
		if(strncmp(buses[i], p->string, sizeof(p->string)) == 0)
			break;
	if(buses[i] == 0)
		return 0;

	if((bus = xalloc(sizeof(Bus))) == nil)
		panic("mkbus: no memory for Bus");
	if(mpbus)
		mpbuslast->next = bus;
	else
		mpbus = bus;
	mpbuslast = bus;

	bus->type = i;
	bus->busno = p->busno;
	if(bus->type == BusEISA){
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
		if(mpeisabus != -1)
			print("mkbus: more than one EISA bus\n");
		mpeisabus = bus->busno;
	}
	else if(bus->type == BusPCI){
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
	}
	else if(bus->type == BusISA){
		bus->po = PcmpHIGH;
		bus->el = PcmpEDGE;
		if(mpisabus != -1)
			print("mkbus: more than one ISA bus\n");
		mpisabus = bus->busno;
	}
	else{
		bus->po = PcmpHIGH;
		bus->el = PcmpEDGE;
	}

	return bus;
}

static Apic*
mkioapic(PCMPioapic* p)
{
	void *va;
	int apicno;
	Apic *apic;

	apicno = p->apicno;
	if(!(p->flags & PcmpEN) || apicno > MaxAPICNO || mpioapic[apicno] != nil)
		return 0;
	/*
	 * Map the I/O APIC.
	 */
	if((va = vmap(p->addr, 1024)) == nil)
		return 0;
	if((apic = xalloc(sizeof(Apic))) == nil)
		panic("mkioapic: no memory for Apic");
	apic->type = PcmpIOAPIC;
	apic->apicno = apicno;
	apic->addr = va;
	apic->paddr = p->addr;
	apic->flags = p->flags;
	mpioapic[apicno] = apic;

	return apic;
}

static Aintr*
mkiointr(PCMPintr* p)
{
	Bus *bus;
	Aintr *aintr;
	PCMPintr* pcmpintr;

	/*
	 * According to the MultiProcessor Specification, a destination
	 * I/O APIC of 0xFF means the signal is routed to all I/O APICs.
	 * It's unclear how that can possibly be correct so treat it as
	 * an error for now.
	 */
	if(p->apicno > MaxAPICNO || mpioapic[p->apicno] == nil)
		return 0;

	if((bus = mpgetbus(p->busno)) == 0)
		return 0;

	if((aintr = xalloc(sizeof(Aintr))) == nil)
		panic("mkiointr: no memory for Aintr");
	aintr->intr = p;

	if(0)
		print("mkiointr: type %d intr type %d flags %#o "
			"bus %d irq %d apicno %d intin %d\n",
			p->type, p->intr, p->flags,
			p->busno, p->irq, p->apicno, p->intin);
	/*
	 * Hack for Intel SR1520ML motherboard, which BIOS describes
	 * the i82575 dual ethernet controllers incorrectly.
	 */
	if(memcmp(pcmp->product, "INTEL   X38MLST     ", 20) == 0){
		if(p->busno == 1 && p->intin == 16 && p->irq == 1){
			if((pcmpintr = xalloc(sizeof(PCMPintr))) == nil)
				panic("iointr: no memory for PCMPintr");
			memmove(pcmpintr, p, sizeof(PCMPintr));
			print("mkiointr: %20.20s bus %d intin %d irq %d\n",
				(char*)pcmp->product,
				pcmpintr->busno, pcmpintr->intin,
				pcmpintr->irq);
			pcmpintr->intin = 17;
			aintr->intr = pcmpintr;
		}
	}
	aintr->apic = mpioapic[p->apicno];
	aintr->next = bus->aintr;
	bus->aintr = aintr;

	return aintr;
}

static int
mklintr(PCMPintr* p)
{
	Apic *apic;
	Bus *bus;
	int i, intin, v;

	/*
	 * The offsets of vectors for LINT[01] are known to be
	 * 0 and 1 from the local APIC vector space at VectorLAPIC.
	 */
	if((bus = mpgetbus(p->busno)) == 0)
		return 0;
	intin = p->intin;

	/*
	 * Pentium Pros have problems if LINT[01] are set to ExtINT
	 * so just bag it, SMP mode shouldn't need ExtINT anyway.
	 */
	if(p->intr == PcmpExtINT || p->intr == PcmpNMI)
		v = ApicIMASK;
	else
		v = mpintrinit(bus, p, VectorLAPIC+intin, p->irq);

	if(p->apicno == 0xFF){
		for(i=0; i<=MaxAPICNO; i++){
			if((apic = mpapic[i]) == nil)
				continue;
			if(apic->flags & PcmpEN)
				apic->lintr[intin] = v;
		}
	}
	else{
		if(apic = mpapic[p->apicno])
			if(apic->flags & PcmpEN)
				apic->lintr[intin] = v;
	}

	return v;
}

static void
dumpmp(uint8_t *p, uint8_t *e)
{
	int i;

	for(i = 0; p < e; p++) {
		if((i % 16) == 0) print("*mp%d=", i/16);
		print("%.2x ", *p);
		if((++i % 16) == 0) print("\n");
	}
	if((i % 16) != 0) print("\n");
}


static void
mpoverride(uint8_t** newp, uint8_t** e)
{
	int size, i, j;
	char buf[20];
	uint8_t* p;
	char* s;

	size = strtol(getconf("*mp"), 0, 0);
	if(size <= 0) panic("mpoverride: invalid size in *mp");
	*newp = p = xalloc(size);
	if(p == nil) panic("mpoverride: can't allocate memory");
	*e = p + size;
	for(i = 0; ; i++){
		snprint(buf, sizeof buf, "*mp%d", i);
		s = getconf(buf);
		if(s == nil) break;
		while(*s){
			j = strtol(s, &s, 16);
			if(*s && *s != ' ' || j < 0 || j > 0xff) panic("mpoverride: invalid entry in %s", buf);
			if(p >= *e) panic("mpoverride: overflow in %s", buf);
			*p++ = j;
		}
	}
	if(p != *e) panic("mpoverride: size doesn't match");
}

static void
pcmpinit(void)
{
	uint8_t *p, *e;
	Apic *apic;
	void *va;

	/*
	 * Map the local APIC.
	 */
	va = vmap(pcmp->lapicbase, 1024);

	print("LAPIC: %.8lux %#p\n", pcmp->lapicbase, va);
	if(va == nil)
		panic("pcmpinit: cannot map lapic %.8lux", pcmp->lapicbase);

	p = ((uint8_t*)pcmp)+PCMPsz;
	e = ((uint8_t*)pcmp)+pcmp->length;
	if(getconf("*dumpmp") != nil)
		dumpmp(p, e);
	if(getconf("*mp") != nil)
		mpoverride(&p, &e);

	/*
	 * Run through the table saving information needed for starting
	 * application processors and initialising any I/O APICs. The table
	 * is guaranteed to be in order such that only one pass is necessary.
	 */
	while(p < e) switch(*p){
	default:
		print("pcmpinit: unknown PCMP type 0x%uX (e-p 0x%zuX)\n",
			*p, e-p);
		while(p < e){
			print("%uX ", *p);
			p++;
		}
		break;

	case PcmpPROCESSOR:
		if(apic = mkprocessor((PCMPprocessor*)p)){
			apic->addr = va;
			apic->paddr = pcmp->lapicbase;
		}
		p += PCMPprocessorsz;
		continue;

	case PcmpBUS:
		mkbus((PCMPbus*)p);
		p += PCMPbussz;
		continue;

	case PcmpIOAPIC:
		if(apic = mkioapic((PCMPioapic*)p))
			ioapicinit(apic, apic->apicno);
		p += PCMPioapicsz;
		continue;

	case PcmpIOINTR:
		mkiointr((PCMPintr*)p);
		p += PCMPintrsz;
		continue;

	case PcmpLINTR:
		mklintr((PCMPintr*)p);
		p += PCMPintrsz;
		continue;
	}

	/*
	 * Ininitalize local APIC and start application processors.
	 */
	mpinit();
}

static void
mpreset(void)
{
	/* stop application processors */
	mpshutdown();

	/* do generic reset */
	archreset();
}

static int identify(void);

PCArch archmp = {
.id=		"_MP_",
.ident=		identify,
.reset=		mpreset,
.intrinit=	pcmpinit,
.intrenable=	mpintrenable,
.intron=	lapicintron,
.introff=	lapicintroff,
.fastclock=	i8253read,
.timerset=	lapictimerset,
};

static int
identify(void)
{
	char *cp;
	_MP_ *_mp_;
	uint32_t len;

	if((cp = getconf("*nomp")) != nil && strcmp(cp, "0") != 0)
		return 1;

	/*
	 * Search for an MP configuration table. For now,
	 * don't accept the default configurations (physaddr == 0).
	 * Check for correct signature, calculate the checksum and,
	 * if correct, check the version.
	 * To do: check extended table checksum.
	 */
	if((_mp_ = sigsearch("_MP_")) == nil || checksum(_mp_, _MP_sz) != 0 || _mp_->physaddr == 0)
		return 1;

	len = PCMPsz;
	if(_mp_->physaddr < MemMin)
		pcmp = KADDR(_mp_->physaddr);
	else if((pcmp = vmap(_mp_->physaddr, len)) == nil)
		return 1;
	if(pcmp->length < len
	|| memcmp(pcmp, "PCMP", 4) != 0
	|| (pcmp->version != 1 && pcmp->version != 4)){
Bad:
		if((uintptr)pcmp < KZERO)
			vunmap(pcmp, len);
		pcmp = nil;
		return 1;
	}
	len = pcmp->length;
	if((uintptr)pcmp < KZERO)
		vunmap(pcmp, PCMPsz);
	if(_mp_->physaddr < MemMin)
		pcmp = KADDR(_mp_->physaddr);
	else if((pcmp = vmap(_mp_->physaddr, len)) == nil)
		return 1;
	if(checksum(pcmp, len) != 0)
		goto Bad;

	if(m->havetsc && getconf("*notsc") == nil)
		archmp.fastclock = tscticks;

	return 0;
}
