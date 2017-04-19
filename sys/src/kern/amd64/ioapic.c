#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "apic.h"
#include "io.h"
#include "adr.h"

typedef struct Rbus Rbus;
typedef struct Rdt Rdt;

struct Rbus {
	Rbus	*next;
	int	bustype;
	int	devno;
	Rdt	*rdt;
};

struct Rdt {
	IOapic	*apic;
	int	intin;
	uint32_t	lo;

	int	ref;				/* could map to multiple busses */
	int	enabled;				/* times enabled */
};

enum {						/* IOAPIC registers */
	Ioregsel	= 0x00,			/* indirect register address */
	Iowin	= 0x04,			/* indirect register data */
	Ioipa		= 0x08,			/* IRQ Pin Assertion */
	Ioeoi		= 0x10,			/* EOI */

	IOapicid	= 0x00,			/* Identification */
	IOapicver	= 0x01,			/* Version */
	IOapicarb	= 0x02,			/* Arbitration */
	Ioabcfg	= 0x03,			/* Boot Configuration */
	Ioredtbl	= 0x10,			/* Redirection Table */
};

static Rdt rdtarray[Nrdt];
static int nrdtarray;
static int gsib;
static Rbus* rdtbus[Nbus];
static Rdt* rdtvecno[IdtMAX+1];
static int dfpolicy = 1;	/* round-robin */

static Lock idtnolock;
static int idtno = IdtIOAPIC;

static	IOapic	xioapic[Napic];
static	int	isabusno = -1;

/* BOTCH: no need for this concept; we've got the bustype */
static void
ioapicisabus(int busno)
{
	if(isabusno != -1){
		if(busno == isabusno)
			return;
		jehanne_print("ioapic: isabus redefined: %d ↛ %d\n", isabusno, busno);
//		return;
	}
	DBG("ioapic: isa busno %d\n", busno);
	isabusno = busno;
}

IOapic*
ioapiclookup(uint32_t id)
{
	IOapic *a;

	if(id > nelem(xioapic))
		return nil;
	a = xioapic + id;
	if(a->useable)
		return a;
	return nil;
}

int
gsitoapicid(int gsi, uint32_t *intin)
{
	int i;
	IOapic *a;

	for(i=0; i<Napic; i++){
		a = xioapic + i;
		if(!a->useable)
			continue;
		if(gsi >= a->gsib && gsi < a->gsib+a->nrdt){
			if(intin != nil)
				*intin = gsi - a->gsib;
			return a - xioapic;
		}
	}
//	jehanne_print("gsitoapicid: no ioapic found for gsi %d\n", gsi);
	return -1;
}

static void
rtblget(IOapic* apic, int sel, uint32_t* hi, uint32_t* lo)
{
	sel = Ioredtbl + 2*sel;

	apic->addr[Ioregsel] = sel+1;
	*hi = apic->addr[Iowin];
	apic->addr[Ioregsel] = sel;
	*lo = apic->addr[Iowin];
}

static void
rtblput(IOapic* apic, int sel, uint32_t hi, uint32_t lo)
{
	sel = Ioredtbl + 2*sel;

	apic->addr[Ioregsel] = sel+1;
	apic->addr[Iowin] = hi;
	apic->addr[Ioregsel] = sel;
	apic->addr[Iowin] = lo;
}

Rdt*
rdtlookup(IOapic *apic, int intin)
{
	int i;
	Rdt *r;

	for(i = 0; i < nrdtarray; i++){
		r = rdtarray + i;
		if(apic == r->apic && intin == r->intin)
			return r;
	}
	return nil;
}

void
ioapicintrinit(int bustype, int busno, int apicno, int intin, int devno, uint32_t lo)
{
	Rbus *rbus;
	Rdt *rdt;
	IOapic *apic;

	if(busno >= Nbus || apicno >= Napic || nrdtarray >= Nrdt)
		return;

	if(bustype == BusISA)
		ioapicisabus(busno);

	apic = &xioapic[apicno];
	if(!apic->useable || intin >= apic->nrdt)
		panic("ioapic: intrinit: usable %d nrdt %d: bus %d apic %d intin %d dev %d lo %.8ux\n",
			apic->useable, apic->nrdt, busno, apicno, intin, devno, lo);

	rdt = rdtlookup(apic, intin);
	if(rdt == nil){
		if(nrdtarray == nelem(rdtarray)){
			jehanne_print("ioapic: intrinit: rdtarray too small\n");
			return;
		}
		rdt = &rdtarray[nrdtarray++];
		rdt->apic = apic;
		rdt->intin = intin;
		rdt->lo = lo;
	}else{
		if(lo != rdt->lo){
			if(bustype == BusISA && intin < 16 && lo == (Im|IPhigh|TMedge)){
				DBG("override: isa %d %.8ux\n", intin, rdt->lo);
				return;	/* expected; default was overridden*/
			}
			jehanne_print("multiple irq botch type %d bus %d %d/%d/%d lo %.8ux vs %.8ux\n",
				bustype, busno, apicno, intin, devno, lo, rdt->lo);
			return;
		}
		DBG("dup rdt %d %d %d %d %.8ux\n", busno, apicno, intin, devno, lo);
	}
	rdt->ref++;
	rbus = jehanne_malloc(sizeof(*rbus));
	rbus->rdt = rdt;
	rbus->bustype = bustype;
	rbus->devno = devno;
	rbus->next = rdtbus[busno];
	rdtbus[busno] = rbus;
}

/*
 * deal with ioapics at the same physical address.  seen on
 * certain supermicro atom systems.  the hope is that only
 * one will be used, and it will be the second one initialized.
 * (the pc kernel ignores this issue.)  it could be that mp and
 * acpi have different numbering?
 */
static IOapic*
dupaddr(uintmem pa)
{
	int i;
	IOapic *p;

	for(i = 0; i < nelem(xioapic); i++){
		p = xioapic + i;
		if(p->paddr == pa)
			return p;
	}
	return nil;
}

IOapic*
ioapicinit(int id, int ibase, uintmem pa)
{
	IOapic *apic, *p;

	/*
	 * Mark the IOAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 */
	if(id >= Napic)
		return nil;
	if((apic = xioapic+id)->useable)
		return apic;

	if((p = dupaddr(pa)) != nil){
		jehanne_print("ioapic%d: same pa as apic%ld\n", id, p-xioapic);
		if(ibase != -1)
			return nil;		/* mp irqs reference mp apic#s */
		apic->addr = p->addr;
	}
	else{
		//adrmapck(pa, 1024, Ammio, Mfree, Cnone);	/* not in adr? */	/* TO DO */
		if((apic->addr = vmap(pa, 1024)) == nil){
			jehanne_print("ioapic%d: can't vmap %#P\n", id, pa);
			return nil;
		}
	}
	apic->useable = 1;
	apic->paddr = pa;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the
	 * responsibility of the O/S to set the APIC ID.
	 */
	lock(apic);
	apic->addr[Ioregsel] = IOapicver;
	apic->nrdt = ((apic->addr[Iowin]>>16) & 0xff) + 1;
	if(ibase != -1)
		apic->gsib = ibase;
	else{
		apic->gsib = gsib;
		gsib += apic->nrdt;
	}
	apic->addr[Ioregsel] = IOapicid;
	apic->addr[Iowin] = id<<24;
	unlock(apic);

	return apic;
}

void
iordtdump(void)
{
	int i;
	Rbus *rbus;
	Rdt *rdt;

	if(!DBGFLG)
		return;
	for(i = 0; i < Nbus; i++){
		if((rbus = rdtbus[i]) == nil)
			continue;
		jehanne_print("iointr bus %d:\n", i);
		for(; rbus != nil; rbus = rbus->next){
			rdt = rbus->rdt;
			jehanne_print(" apic %ld devno %#ux (%d %d) intin %d lo %#ux ref %d\n",
				rdt->apic-xioapic, rbus->devno, rbus->devno>>2,
				rbus->devno & 0x03, rdt->intin, rdt->lo, rdt->ref);
		}
	}
}

void
ioapicdump(void)
{
	int i, n;
	IOapic *apic;
	uint32_t hi, lo;

	if(!DBGFLG)
		return;
	for(i = 0; i < Napic; i++){
		apic = &xioapic[i];
		if(!apic->useable || apic->addr == 0)
			continue;
		jehanne_print("ioapic %d addr %#p nrdt %d ibase %d\n",
			i, apic->addr, apic->nrdt, apic->gsib);
		for(n = 0; n < apic->nrdt; n++){
			lock(apic);
			rtblget(apic, n, &hi, &lo);
			unlock(apic);
			jehanne_print(" rdt %2.2d %#8.8ux %#8.8ux\n", n, hi, lo);
		}
	}
}

static char*
ioapicprint(char *p, char *e, IOapic *a, int i)
{
	char *s;

	s = "ioapic";
	p = jehanne_seprint(p, e, "%-8s ", s);
	p = jehanne_seprint(p, e, "%8ux ", i);
	p = jehanne_seprint(p, e, "%6d ", a->gsib);
	p = jehanne_seprint(p, e, "%6d ", a->gsib+a->nrdt-1);
	p = jehanne_seprint(p, e, "%#P ", a->paddr);
	p = jehanne_seprint(p, e, "\n");
	return p;
}

static long
ioapicread(Chan* _1, void *a, long n, int64_t off)
{
	char *s, *e, *p;
	long i, r;

	s = jehanne_malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < nelem(xioapic); i++)
		if(xioapic[i].useable)
			p = ioapicprint(p, e, xioapic + i, i);
	r = -1;
	if(!waserror()){
		r = readstr(off, a, n, s);
		poperror();
	}
	jehanne_free(s);
	return r;
}

void
ioapiconline(void)
{
	int i;
	IOapic *apic;

	addarchfile("ioapic", 0444, ioapicread, nil);
	for(apic = xioapic; apic < &xioapic[Napic]; apic++){
		if(!apic->useable || apic->addr == nil)
			continue;
		for(i = 0; i < apic->nrdt; i++){
			lock(apic);
			rtblput(apic, i, 0, Im);
			unlock(apic);
		}
	}
	jehanne_print("init ioapic dump\n");
	ioapicdump();
}

static int
ioapicintrdd(uint32_t* hi, uint32_t* lo)
{
	Lapic *lapic;
	Mach *mach;
	int i;
	static int df;

	/*
	 * Set delivery mode (lo) and destination field (hi)
	 *
	 * Currently, assign each interrupt to a different CPU
	 * using physical mode delivery.  Using the topology
	 * (packages/cores/threads) could be helpful.
	 */
	switch(dfpolicy){
	case 0:
		i = sys->machptr[0]->apicno;
		break;
	default:	/* round-robin */
		for(;;){
			i = df;
			if(++df >= Napic)
				df = 0;
			if((lapic = lapiclookup(i)) != nil &&
			   (mach = sys->machptr[lapic->machno]) != nil &&
			   mach->online)
				break;
		}
		break;
	}
	*hi = i<<24;
	*lo |= Pm|MTf;
	return i;
}

int
nextvec(void)
{
	uint32_t vecno;

	lock(&idtnolock);
	vecno = idtno;
	idtno = (idtno+8) % IdtMAX;
	if(idtno < IdtIOAPIC)
		idtno += IdtIOAPIC;
	unlock(&idtnolock);

	return vecno;
}

static int
msimask(Vctl *v, int mask)
{
	Pcidev *p;

	p = pcimatchtbdf(v->tbdf);
	if(p == nil)
		return -1;
	return pcimsimask(p, mask);
}

static int
intrenablemsi(Vctl* v, Pcidev *p)
{
	uint32_t vno, lo, hi;
	uint64_t msivec;

	vno = nextvec();

	lo = IPlow | TMedge | vno;
	v->affinity = ioapicintrdd(&hi, &lo);

	if(lo & Lm)
		lo |= MTlp;

	msivec = (uint64_t)hi<<32 | lo;
	if(pcimsienable(p, msivec) == -1)
		return -1;
	v->isr = lapicisr;
	v->eoi = lapiceoi;
	v->vno = vno;
	v->type = "msi";
	v->mask = msimask;

	DBG("msiirq: %T: enabling %.16llux %s irq %d vno %d\n", p->tbdf, msivec, v->name, v->irq, vno);
	return vno;
}

int
disablemsi(Vctl* _1, Pcidev *p)
{
	if(p == nil)
		return -1;
	return pcimsimask(p, 1);
}

int
ioapicintrenable(Vctl* v)
{
	Rbus *rbus;
	Rdt *rdt;
	uint32_t hi, lo;
	int bustype, busno, devno, vecno;

	if(v->tbdf == BUSUNKNOWN){
		if(v->irq >= IdtLINT0 && v->irq <= IdtMAX){
			if(v->irq != IdtSPURIOUS)
				v->isr = lapiceoi;
			v->type = "lapic";
			return v->irq;
		}
		else{
			/*
			 * Legacy ISA.
			 * Make a busno and devno using the
			 * ISA bus number and the irq.
			 */
			if(isabusno == -1)
				panic("no ISA bus allocated");
			busno = isabusno;
			devno = v->irq;
			bustype = BusISA;
		}
	}
	else if((bustype = BUSTYPE(v->tbdf)) == BusPCI){
		/*
		 * PCI.
		 * Make a devno from BUSDNO(tbdf) and pcidev->intp.
		 */
		Pcidev *pcidev;

		busno = BUSBNO(v->tbdf);
		if((pcidev = pcimatchtbdf(v->tbdf)) == nil)
			panic("no PCI dev for tbdf %T", v->tbdf);
		if((vecno = intrenablemsi(v, pcidev)) != -1)
			return vecno;
		disablemsi(v, pcidev);
		if((devno = pcicfgr8(pcidev, PciINTP)) == 0)
			panic("no INTP for tbdf %T", v->tbdf);
		devno = BUSDNO(v->tbdf)<<2|(devno-1);
		DBG("ioapicintrenable: tbdf %T busno %d devno %d\n",
			v->tbdf, busno, devno);
	}
	else{
		SET(busno, devno);
		panic("unknown tbdf %T", v->tbdf);
	}
	rdt = nil;
	for(rbus = rdtbus[busno]; rbus != nil; rbus = rbus->next)
		if(rbus->devno == devno && rbus->bustype == bustype){
			rdt = rbus->rdt;
			break;
		}
	if(rdt == nil){
		/*
		 * PCI devices defaulted to ISA (ACPI).
		 */
		if((busno = isabusno) == -1)
			return -1;
		devno = v->irq;
		for(rbus = rdtbus[busno]; rbus != nil; rbus = rbus->next)
			if(rbus->devno == devno){
				rdt = rbus->rdt;
				break;
			}
		DBG("isa: tbdf %T busno %d devno %d %#p\n",
			v->tbdf, busno, devno, rdt);
	}
	if(rdt == nil)
		return -1;

	/*
	 * Assume this is a low-frequency event so just lock
	 * the whole IOAPIC to initialise the RDT entry
	 * rather than putting a Lock in each entry.
	 */
	lock(rdt->apic);
	DBG("%T: %ld/%d/%d (%d)\n", v->tbdf, rdt->apic - xioapic, rbus->devno, rdt->intin, devno);
	if((rdt->lo & 0xff) == 0){
		vecno = nextvec();
		rdt->lo |= vecno;
		rdtvecno[vecno] = rdt;
	}else
		DBG("%T: mutiple irq bus %d dev %d\n", v->tbdf, busno, devno);

	rdt->enabled++;
	lo = (rdt->lo & ~Im);
	v->affinity = ioapicintrdd(&hi, &lo);
	rtblput(rdt->apic, rdt->intin, hi, lo);
	vecno = lo & 0xff;
	unlock(rdt->apic);

	DBG("busno %d devno %d hi %#.8ux lo %#.8ux vecno %d\n",
		busno, devno, hi, lo, vecno);
	v->isr = lapicisr;
	v->eoi = lapiceoi;
	v->vno = vecno;
	v->type = "ioapic";

	return vecno;
}

int
ioapicintrdisable(int vecno)
{
	Rdt *rdt;

	/*
	 * FOV. Oh dear. This isn't very good.
	 * Fortunately rdtvecno[vecno] is static
	 * once assigned.
	 * Must do better.
	 *
	 * What about any pending interrupts?
	 */
	if(vecno < 0 || vecno > IdtMAX){
		panic("ioapicintrdisable: vecno %d out of range", vecno);
		return -1;
	}
	if((rdt = rdtvecno[vecno]) == nil){
		panic("ioapicintrdisable: vecno %d has no rdt", vecno);
		return -1;
	}

	lock(rdt->apic);
	rdt->enabled--;
	if(rdt->enabled == 0)
		rtblput(rdt->apic, rdt->intin, 0, rdt->lo);
	unlock(rdt->apic);

	return 0;
}
