#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "adr.h"

#include "apic.h"
#include "acpi.h"
#include <aml.h>

typedef struct Rsd Rsd;

struct Rsd {
	uint8_t	sig[8];
	uint8_t	csum;
	uint8_t	oemid[6];
	uint8_t	rev;
	uint8_t	raddr[4];
	uint8_t	len[4];
	uint8_t	xaddr[8];
	uint8_t	xcsum;
	uint8_t	reserved[3];
};

enum {
	Tblsz	= 4+4+1+1+6+8+4+4+4,
	Rdsz	= 8+1+6+1+4+4+8+1+3,
};

static	Rsd	*rsd;
static	int	ntblpa;			/* physical addresses visited by maptable() */
static	uintmem	tblpa[64];
static	int	ntblmap;			/* successfully mapped tables */
static	Tbl	*tblmap[64];
	Fadt	fadt;
	Acpicfg	acpicfg;

static int
checksum(void *v, int n)
{
	uint8_t *p, s;

	s = 0;
	p = v;
	while(n-- > 0)
		s += *p++;
	return s;
}

static uint32_t
get16(uint8_t *p)
{
	return (p[1]<<8)|p[0];
}

static uint32_t
get32(uint8_t *p)
{
	return (p[3]<<24)|(p[2]<<16)|(p[1]<<8)|p[0];
}

static uint64_t
get64(uint8_t *p)
{
	return ((uint64_t)get32(p+4))|get32(p);
}

static void
put16(uint8_t *p, int v)
{
	p[0] = v;
	p[1] = v>>8;
}

static void
put32(uint8_t *p, int v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static uint32_t
tbldlen(Tbl *t)
{
	return get32(t->len) - Tblsz;
}

Tbl*
acpigettbl(void *sig)
{
	int i;
	for(i=0; i<ntblmap; i++)
		if(memcmp(tblmap[i]->sig, sig, 4) == 0)
			return tblmap[i];
	return nil;
}

#define	vunmap(a,b)
#define	vmapoverlap	vmap

static void
acpimaptbl(uint64_t xpa)
{
	uint8_t *p, *e;
	int i;
	uintmem pa;
	uint32_t l;
	Tbl *t;

	pa = xpa;
	if(pa != xpa || pa == 0)
		return;
	if(ntblpa >= nelem(tblpa) || ntblmap >= nelem(tblmap))
		return;
	for(i=0; i<ntblpa; i++){
		if(pa == tblpa[i])
			return;
	}
	tblpa[ntblpa++] = pa;
	if((t = vmapoverlap(pa, 8)) == nil)
		return;
	l = get32(t->len);
	if(l < Tblsz){
		vunmap(t, 8);
		return;
	}
	vunmap(t, 8);
	if((t = vmapoverlap(pa, l)) == nil)
		return;
	if(checksum(t, l)){
		vunmap(t, l);
		return;
	}
	tblmap[ntblmap++] = t;

	p = (uint8_t*)t;
	e = p + l;
	if(memcmp("RSDT", t->sig, 4) == 0){
		for(p = t->data; p+3 < e; p += 4)
			acpimaptbl(get32(p));
	}
	else if(memcmp("XSDT", t->sig, 4) == 0){
		for(p = t->data; p+7 < e; p += 8)
			acpimaptbl(get64(p));
	}
	else if(memcmp("FACP", t->sig, 4) == 0){
		if(l < 44)
			return;
		acpimaptbl(get32(p + 40));
		if(l < 148)
			return;
		acpimaptbl(get64(p + 140));
	}
}

static void*
rsdscan(uint8_t* addr, int len, char* signature)
{
	int sl;
	uint8_t *e, *p;

	e = addr+len;
	sl = strlen(signature);
	for(p = addr; p+sl < e; p += 16){
		if(memcmp(p, signature, sl))
			continue;
		return p;
	}

	return nil;
}

static void*
rsdsearch(char* signature)
{
	uintptr_t p;
	uint8_t *bda;
	Rsd *rsd;

	/*
	 * Search for the data structure signature:
	 * 1) in the first KB of the EBDA;
	 * 2) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	if(strncmp((char*)KADDR(0xFFFD9), "EISA", 4) == 0){
		bda = BIOSSEG(0x40);
		if((p = (bda[0x0F]<<8)|bda[0x0E])){
			if(rsd = rsdscan(KADDR(p), 1024, signature))
				return rsd;
		}
	}
	return rsdscan(BIOSSEG(0xE000), 0x20000, signature);
}

static void
rsdload(void)
{
	if((rsd = rsdsearch("RSD PTR ")) == nil)
		panic("acpi: no rsd ptr");
	if(checksum(rsd, 20) && checksum(rsd, 36))
		panic("acpi: acpi checksum");
}

static void
maptables(void)
{
	if(ntblmap > 0 || ntblpa > 0)
		return;
	rsdload();
	if(rsd->rev >= 2 && !checksum(rsd, 36))
		acpimaptbl(get64(rsd->xaddr));
	else if(!checksum(rsd, 20))
		acpimaptbl(get32(rsd->raddr));
}

enum {
	Iointr,
	Lintr,

	MTint	= 0,		/* fake interrupt type, equivalent to fixed */
};

static uint32_t
apicmkintr(uint32_t src, uint32_t inttype, int polarity, int trigger, uint32_t apicno, uint32_t intin)
{
	uint32_t v;
	IOapic *ioapic;
	Lapic *lapic;

	/*
	 * Check valid bus, interrupt input pin polarity
	 * and trigger mode. If the APIC ID is 0xff it means
	 * all APICs of this type so those checks for useable
	 * APIC and valid INTIN must also be done later in
	 * the appropriate init routine in that case. It's hard
	 * to imagine routing a signal to all IOAPICs, the
	 * usual case is routing NMI and ExtINT to all LAPICs.
	 */
	if(apicno != 0xff){
		if(Napic < 256 && apicno >= Napic){
			print("apic: id out-of-range: %d\n", apicno);
			return 0;
		}
		switch(src){
		default:
			print("apic: intin botch: %d\n", intin);
			return 0;
		case Iointr:
			if((ioapic = ioapiclookup(apicno)) == nil){
				print("ioapic%d: ioapic unusable\n", apicno);
				return 0;
			}
			if(intin >= ioapic->nrdt){
				print("ioapic%d: intin %d >= nrdt %d\n", apicno, intin, ioapic->nrdt);
				return 0;
			}
			break;
		case Lintr:
			if((lapic = lapiclookup(apicno)) == nil){
				print("lapic%d: lapic unusable\n", apicno);
				return 0;
			}
			if(intin >= nelem(lapic->lvt)){
				print("lapic%d: intin beyond lvt: %d\n", apicno, intin);
				return 0;
			}
			USED(lapic);
			break;
		}
	}

	/*
	 * Create the low half of the vector table entry (LVT or RDT).
	 * For the NMI, SMI and ExtINT cases, the polarity and trigger
	 * are fixed (but are not always consistent over IA-32 generations).
	 * For the INT case, either the polarity/trigger are given or
	 * it defaults to that of the source bus;
	 * whether INT is Fixed or Lowest Priority is left until later.
	 */
	v = Im;
	switch(inttype){
	default:
		print("apic: bad irq type %d\n", inttype);
		return 0;
	case MTint:				/* INT (fake type, same as fixed) */
		v |= polarity | trigger;
		break;
	case MTnmi:				/* NMI */
	case MTsmi:				/* SMI */
	case MTei:				/* ExtINT */
		v |= TMedge|IPhigh|inttype;
		break;
	}

	return v;
}

int
flagstopolarity(int bustype, int flags)
{
	switch(flags & 3){
	case 1:
		return IPhigh;
	case 3:
		return IPlow;
	case 2:
		return -1;
	}
	switch(bustype){
	case BusISA:
		return IPhigh;
	case BusPCI:
		return IPlow;
		break;
	default:
		return -1;
	}
}

int
flagstotrigger(int bustype, int flags)
{
	switch((flags>>2) & 3){
	case 1:
		return TMedge;
	case 3:
		return TMlevel;
	case 2:
		return -1;
	}
	switch(bustype){
	case BusISA:
		return TMedge;
	case BusPCI:
		return TMlevel;
		break;
	default:
		return -1;
	}
}

static void
addirq(int gsi, int bustype, int busno, int irq, int flags)
{
	uint32_t apicno, intin, polarity, trigger;
	uint32_t i;

	if((apicno = gsitoapicid(gsi, &intin)) == -1){
		DBG("acpi: addirq: no apic for gsi %d bus type=%d busno %d\n", gsi, bustype, busno);
		return;
	}
	DBG("addirq: gsi %d %s busno %d irq %d flags %.8ux\n",
		gsi, bustype == BusPCI? "pci": "isa", busno, irq, flags);
	polarity = flagstopolarity(bustype, flags);
	trigger = flagstotrigger(bustype, flags);
	if(polarity == -1 || trigger == -1){
		print("addirq: bad polarity: gsi %d %s busno %d irq %d flags %.8ux\n",
			gsi, bustype == BusPCI? "pci": "isa", busno, irq, flags);
		return;
	}

	i = apicmkintr(Iointr, MTint, polarity, trigger, apicno, intin);
	ioapicintrinit(bustype, busno, apicno, intin, irq, i);
}

static char*
eisaid(void *v)
{
	uint32_t b, l;
	int i;
	static char id[8];

	if(amltag(v) == 's')
		return v;
	b = amlint(v);
	for(l = 0, i=24; i>=0; i -= 8, b >>= 8)
		l |= (b & 0xFF) << i;
	id[7] = 0;
	for(i=6; i>=3; i--, l >>= 4)
		id[i] = "0123456789ABCDEF"[l & 0xF];
	for(i=2; i>=0; i--, l >>= 5)
		id[i] = '@' + (l & 0x1F);
	return id;
}

/*static*/ int
pcibusno(void *dot)
{
	int bno, adr, tbdf;
	Pcidev *pdev;
	void *p, *x;
	char *id;

	id = nil;
	if((x = amlwalk(dot, "^_HID")) != nil)
		if((p = amlval(x)) != nil)
			id = eisaid(p);
	if((x = amlwalk(dot, "^_BBN")) == nil)
		if((x = amlwalk(dot, "^_ADR")) == nil)
			return -1;
	if((p = amlval(x)) == nil)
		return -1;
	adr = amlint(p);
	/* if root bridge, then we are done here */
	if(id != nil && (strcmp(id, "PNP0A03")==0 || strcmp(id, "PNP0A08")==0))
		return adr;
	x = amlwalk(dot, "^");
	if(x == nil || x == dot)
		return -1;
	if((bno = pcibusno(x)) < 0)
		return -1;
	tbdf = MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	pdev = pcimatchtbdf(tbdf);
	if(pdev == nil)
		return -1;
	if(pdev->bridge == nil)
		return bno;
	return BUSBNO(pdev->bridge->tbdf);
}

static int
getirqs(void *d, uint8_t pmask[32], int *pflags)
{
	int i, n, m;
	uint8_t *p;

	*pflags = 0;
	memset(pmask, 0, 32);
	if(amltag(d) != 'b')
		return -1;
	p = amlval(d);
	if(amllen(d) >= 2 && (p[0] == 0x22 || p[0] == 0x23)){
		pmask[0] = p[1];
		pmask[1] = p[2];
		if(amllen(d) >= 3 && p[0] == 0x23)
			*pflags = p[3];
		return 0;
	}
	if(amllen(d) >= 5 && p[0] == 0x89){
		n = p[4];
		if(amllen(d) < 5+n*4)
			return -1;
		for(i=0; i<n; i++){
			m = get32(p+5 + i*4);
			if(m >= 0 && m < 256)
				pmask[m/8] |= 1<<(m%8);
		}
		*pflags = p[3];
		return 0;
	}
	return -1;
}

static uint8_t*
setirq(void *d, uint32_t irq)
{
	uint8_t *p;

	if(amltag(d) != 'b')
		return nil;
	p = amlnew('b', amllen(d));
	memmove(p, d, amllen(p));
	if(p[0] == 0x22 || p[0] == 0x23)
		put16(p, 1<<irq);
	if(p[0] == 0x89){
		p[4] = 1;
		put32(p+5, irq);
	}
	return p;
}

static int
setuplink(void *link, int *pflags)
{
	uint8_t im, pm[32], cm[32], *c;
	static int lastirq = 1;
	int gsi, i;
	void *r;

	if(amltag(link) != 'N')
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_PRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, pm, pflags) < 0)
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_CRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, cm, pflags) < 0)
		return -1;
	
	gsi = -1;
	for(i=0; i<256; i++){
		im = 1<<(i%8);
		if(pm[i/8] & im){
			if(cm[i/8] & im)
				gsi = i;
		}
	}

	if(gsi > 0 || getconf("*nopcirouting") != nil)
		return gsi;

	for(i=0; i<256; i++){
		gsi = lastirq++ & 0xFF;	/* round robin */
		im = 1<<(gsi%8);
		if(pm[gsi/8] & im){
			if((c = setirq(r, gsi)) == nil)
				break;
			if(amleval(amlwalk(link, "_SRS"), "b", c, nil) < 0)
				break;
			return gsi;
		}
	}
	return -1;
}

static int
enumprt(void *dot, void * _1)
{
	void *p, **a, **b;
	int bno, dno, pin, gsi, flags, n, i;

	bno = pcibusno(dot);
	if(bno < 0){
		DBG("enumprt: pci not found %V\n", dot);
		return 1;
	}

	/* evalulate _PRT method */
	p = nil;
	if(amleval(dot, "", &p) < 0)
		return 1;
	if(amltag(p) != 'p')
		return 1;

	amltake(p);
	n = amllen(p);
	a = amlval(p);
	for(i=0; i<n; i++){
		if(amltag(a[i]) != 'p')
			continue;
		if(amllen(a[i]) != 4)
			continue;
		flags = 0;
		b = amlval(a[i]);
		dno = amlint(b[0])>>16;
		pin = amlint(b[1]);
		gsi = amlint(b[3]);
		if(gsi == 0){
			gsi = setuplink(b[2], &flags);
			if(gsi <= 0)
				continue;
		}
		addirq(gsi, BusPCI, bno, (dno<<2)|pin, 0);
	}
	amldrop(p);
	return 1;
}

static void
loadtbls(char *name, int all)
{
	int i;
	Tbl *t;

	for(i = 0; i < ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, name, 4) == 0){
			amlload(t->data, tbldlen(t));
			if(!all)
				break;
		}
	}
}

static long
readtbls(Chan* _1, void *v, long n, int64_t o)
{
	int i, l, m;
	uint8_t *p;
	Tbl *t;

	maptables();

	p = v;
	for(i=0; n > 0 && i < ntblmap; i++){
		t = tblmap[i];
		l = get32(t->len);
		if(o >= l){
			o -= l;
			continue;
		}
		m = l - o;
		if(m > n)
			m = n;
		memmove(p, (uint8_t*)t + o, m);
		p += m;
		n -= m;
		o = 0;
	}
	return p - (uint8_t*)v;
}

enum {
	Lapicen		= 1,
};

typedef struct Parsedat Parsedat;
struct Parsedat {
	int	maxmach;		/* for the apic structure */
};

static void
parseapic(Tbl *t, Parsedat *dat)
{
	uint8_t *p, *e;
	int i, c, nmach, maxmach;
	uintmem lapicbase;

	maxmach = dat->maxmach;

	/* set APIC mode */
	amleval(amlwalk(amlroot, "_PIC"), "i", 1, nil);

	p = t->data;
	e = p + tbldlen(t);
	lapicbase = get32(p);
	p += 8;

	nmach = 0;
	for(; p < e; p += c){
		c = p[1];
		if(c < 2 || (p+c) > e)
			break;
		switch(*p){
		case 0x00:	/* Processor Local APIC */
			if(p[4] & Lapicen && nmach < maxmach){
				lapicinit(p[3], lapicbase, nmach==0);
				++nmach;
			}
			break;
		case 0x01:	/* I/O APIC */
			ioapicinit(p[2], get32(p+8), get32(p+4));
			break;
		case 0x02:	/* Interrupt Source Override */
			addirq(get32(p+4), BusISA, 0, p[3], get16(p+8));
			break;
		case 0x03:	/* NMI Source */
			print("acpi: ignoring nmi source\n");
			break;
		case 0x04:	/* Local APIC NMI */
			DBG("acpi: lapic nmi %.2ux flags %.4ux lint# %d (ignored)\n",
				p[2], (uint32_t)get16(p+3), p[5]);
			break;
		case 0x05:	/* Local APIC Address Override */
		case 0x06:	/* I/O SAPIC */
		case 0x07:	/* Local SAPIC */
		case 0x08:	/* Platform Interrupt Sources */
		case 0x09:	/* Processor Local x2APIC */
		case 0x0A:	/* x2APIC NMI */
		case 0x0B:	/* GIC */
		case 0x0C:	/* GICD */
			print("acpi: ignoring entry: %.2ux\n", *p);
			break;
		}
	}

	/* look for PCI interrupt mappings */
	amlenum(amlroot, "_PRT", enumprt, nil);

	/* add identity mapped legacy isa interrupts */
	for(i=0; i<16; i++)
		addirq(i, BusISA, 0, i, 0);

	DBG("acpiinit: %d maches\n", nmach);
}

static void
parsesrat(Tbl *t, Parsedat* _1)
{
	uint8_t *p, *e;

	e = t->data + tbldlen(t);
	for(p = t->data + 12; p < e; p += p[1]){
		switch(p[0]){
		case 0:		/* local apic */
			if(get32(p+4)&1)
				lapicsetdom(p[3], p[2] | p[9]<<24| p[10]<<16 | p[11]<<8);
			break;
		case 1:		/* memory affinity */
			if(get32(p+28)&1)
				memaffinity(get64(p+8), get64(p+16), get32(p+2), p[0]);
			break;
		case 2:		/* x2apic */
			if(get32(p+12)&1)
				lapicsetdom(get32(p+8), get32(p+4));
			break;
		default:
			print("acpi: SRAT type %.2ux unknown\n", p[0]);
			break;
		}
	}
}

static char* regnames[] = {
	"mem", "io", "pcicfg", "embed",
	"smb", "cmos", "pcibar",
};

static int
Gfmt(Fmt* f)
{
	Gas *g;

	g = va_arg(f->args, Gas*);
	switch(g->spc){
	case MemSpace:
	case IoSpace:
	case EbctlSpace:
	case SmbusSpace:
	case CmosSpace:
	case PcibarSpace:
	case IpmiSpace:
		fmtprint(f, "[%s", regnames[g->spc]);
		break;
	case PcicfgSpace:
		fmtprint(f, "[pci %T", (int)g->addr);
		break;
//	case FixedhwSpace:		// undeclared
//		fmtprint(f, "[hw");
//		break;
	default:
		fmtprint(f, "[%#ux", g->spc);
		break;
	}
	fmtprint(f, " %#llux", g->addr);
	if(g->off != 0)
		fmtprint(f, "+%d", g->off);
	fmtprint(f, " len %d", g->len);
	if(g->accsz != 0)
		fmtprint(f, " accsz %d", g->accsz);
	return fmtprint(f, "]");
}

static void
gasget(Gas *gas, uint8_t *p)
{
	gas->spc = p[0];
	gas->len = p[1];
	gas->off = p[2];
	gas->accsz = p[3];
	gas->addr = get64(p+4);
}

static int
loadfacs(uintmem pa)
{
	USED(pa);
	return 0;
}

static long
readfadt(Chan* _1, void *a, long n, int64_t o)
{
	char *s, *p, *e;
	Fadt *f;

	s = smalloc(READSTR);
	if(waserror()){
		free(s);
		nexterror();
	}
	p = s;
	e = s+READSTR;
	f = &fadt;

	p = seprint(p, e, "facs %#ux\n", f->facs);
	p = seprint(p, e, "dsdt %#ux\n", f->dsdt);
	p = seprint(p, e, "pmprofile %#ux\n", f->pmprofile);
	p = seprint(p, e, "sciint %d\n", f->sciint);
	p = seprint(p, e, "smicmd %#ux\n", f->smicmd);
	p = seprint(p, e, "acpienable %#ux\n", f->acpienable);
	p = seprint(p, e, "acpidisable %#ux\n", f->acpidisable);
	p = seprint(p, e, "s4biosreq %#ux\n", f->s4biosreq);
	p = seprint(p, e, "pstatecnt %#ux\n", f->pstatecnt);
	p = seprint(p, e, "pm1aevtblk %#ux\n", f->pm1aevtblk);
	p = seprint(p, e, "pm1bevtblk %#ux\n", f->pm1bevtblk);
	p = seprint(p, e, "pm1acntblk %#ux\n", f->pm1acntblk);
	p = seprint(p, e, "pm1bcntblk %#ux\n", f->pm1bcntblk);
	p = seprint(p, e, "pm2cntblk %#ux\n", f->pm2cntblk);
	p = seprint(p, e, "pmtmrblk %#ux\n", f->pmtmrblk);
	p = seprint(p, e, "gpe0blk %#ux\n", f->gpe0blk);
	p = seprint(p, e, "gpe1blk %#ux\n", f->gpe1blk);
	p = seprint(p, e, "pm1evtlen %#ux\n", f->pm1evtlen);
	p = seprint(p, e, "pm1cntlen %#ux\n", f->pm1cntlen);
	p = seprint(p, e, "pm2cntlen %#ux\n", f->pm2cntlen);
	p = seprint(p, e, "pmtmrlen %#ux\n", f->pmtmrlen);
	p = seprint(p, e, "gpe0blklen %#ux\n", f->gpe0blklen);
	p = seprint(p, e, "gpe1blklen %#ux\n", f->gpe1blklen);
	p = seprint(p, e, "gp1base %#ux\n", f->gp1base);
	p = seprint(p, e, "cstcnt %#ux\n", f->cstcnt);
	p = seprint(p, e, "plvl2lat %#ux\n", f->plvl2lat);
	p = seprint(p, e, "plvl3lat %#ux\n", f->plvl3lat);
	p = seprint(p, e, "flushsz %#ux\n", f->flushsz);
	p = seprint(p, e, "flushstride %#ux\n", f->flushstride);
	p = seprint(p, e, "dutyoff %#ux\n", f->dutyoff);
	p = seprint(p, e, "dutywidth %#ux\n", f->dutywidth);
	p = seprint(p, e, "dayalrm %#ux\n", f->dayalrm);
	p = seprint(p, e, "monalrm %#ux\n", f->monalrm);
	p = seprint(p, e, "century %#ux\n", f->century);
	p = seprint(p, e, "iapcbootarch %#ux\n", f->iapcbootarch);
	p = seprint(p, e, "flags %#ux\n", f->flags);
	p = seprint(p, e, "resetreg %G\n", &f->resetreg);
	if(f->rev >= 3){
		p = seprint(p, e, "resetval %#ux\n", f->resetval);
		p = seprint(p, e, "xfacs %#llux\n", f->xfacs);
		p = seprint(p, e, "xdsdt %#llux\n", f->xdsdt);
		p = seprint(p, e, "xpm1aevtblk %G\n", &f->xpm1aevtblk);
		p = seprint(p, e, "xpm1bevtblk %G\n", &f->xpm1bevtblk);
		p = seprint(p, e, "xpm1acntblk %G\n", &f->xpm1acntblk);
		p = seprint(p, e, "xpm1bcntblk %G\n", &f->xpm1bcntblk);
		p = seprint(p, e, "xpm2cntblk %G\n", &f->xpm2cntblk);
		p = seprint(p, e, "xpmtmrblk %G\n", &f->xpmtmrblk);
		p = seprint(p, e, "xgpe0blk %G\n", &f->xgpe0blk);
		p = seprint(p, e, "xgpe1blk %G\n", &f->xgpe1blk);
	}
	USED(p);

	n = readstr(o, a, n, s);
	poperror();
	free(s);
	return n;
}

static void
parsefadt(Tbl *t, Parsedat* _1)
{
	uint8_t *p;
	Fadt *f;

	p = (uint8_t*)t;
	f = &fadt;
	f->rev = t->rev;
	f->facs = get32(p + 36);
	f->dsdt = get32(p + 40);
	f->pmprofile = p[45];
	f->sciint = get16(p+46);
	f->smicmd = get32(p+48);
	f->acpienable = p[52];
	f->acpidisable = p[53];
	f->s4biosreq = p[54];
	f->pstatecnt = p[55];
	f->pm1aevtblk = get32(p+56);
	f->pm1bevtblk = get32(p+60);
	f->pm1acntblk = get32(p+64);
	f->pm1bcntblk = get32(p+68);
	f->pm2cntblk = get32(p+72);
	f->pmtmrblk = get32(p+76);
	f->gpe0blk = get32(p+80);
	f->gpe1blk = get32(p+84);
	f->pm1evtlen = p[88];
	f->pm1cntlen = p[89];
	f->pm2cntlen = p[90];
	f->pmtmrlen = p[91];
	f->gpe0blklen = p[92];
	f->gpe1blklen = p[93];
	f->gp1base = p[94];
	f->cstcnt = p[95];
	f->plvl2lat = get16(p+96);
	f->plvl3lat = get16(p+98);
	f->flushsz = get16(p+100);
	f->flushstride = get16(p+102);
	f->dutyoff = p[104];
	f->dutywidth = p[105];
	f->dayalrm = p[106];
	f->monalrm = p[107];
	f->century = p[108];
	f->iapcbootarch = get16(p+109);
	f->flags = get32(p+112);
	gasget(&f->resetreg, p+116);

	if(f->rev >= 3){
		f->resetval = p[128];
		f->xfacs = get64(p+132);
		f->xdsdt = get64(p+140);
		gasget(&f->xpm1aevtblk, p+148);
		gasget(&f->xpm1bevtblk, p+160);
		gasget(&f->xpm1acntblk, p+172);
		gasget(&f->xpm1bcntblk, p+184);
		gasget(&f->xpm2cntblk, p+196);
		gasget(&f->xpmtmrblk, p+208);
		gasget(&f->xgpe0blk, p+220);
		gasget(&f->xgpe1blk, p+232);
	}

//	dumpfadt(f);
	if(f->xfacs != 0)
		loadfacs(f->xfacs);
	else
		loadfacs(f->facs);
}

typedef	struct	Hpet	Hpet;
struct Hpet {
	uint8_t	id[4];
	uint8_t	addr[12];		/* gas */
	uint8_t	seqno;
	uint8_t	minticks[2];
	uint8_t	attr;			/* Page Protection */
};

static void
parsehpet(Tbl *t, Parsedat* _1)
{
	int minticks;
	Hpet *h;
	Gas g;

	h = (Hpet*)t->data;
	gasget(&g, h->addr);
	minticks = get16(h->minticks);

	DBG("acpi: hpet id %#ux addr %d %d %d %d %#p seqno %d ticks %d attr %#ux\n",
		get32(h->id), g.spc, g.len, g.off, g.accsz,
		g.addr, h->seqno, minticks, h->attr);
	hpetinit(get32(h->id), h->seqno, g.addr, minticks);
}

typedef struct Mcfg Mcfg;
struct Mcfg
{
	uint64_t	base;	/* base of enhanced configuration mechanism */
	uint16_t	pciseg;	/* pci segment group number */
	uint8_t	start;		/* first pci bus number covered by this */
	uint8_t	end;		/* last pci bus number covered */
	/* 4 reserved */
};
static Mcfg mcfg[8];
static int	nmcfg;

static void
parsemcfg(Tbl *t, Parsedat* _1)
{
	uint8_t *p, *e;
	Mcfg *mc;

	if(nmcfg == nelem(mcfg))
		return;
	mc = &mcfg[nmcfg++];
	e = t->data + tbldlen(t);
	p = t->data + 8;	/* reserved */
//	mc->nbus = 0;
	for(; p < e; p += 16){
		mc->base = get64(p+0);
		mc->pciseg = get16(p+8);
		mc->start = p[10];
		mc->end = p[11];
print("MCFG: %d-%d %d %#P\n", mc->start, mc->end, mc->pciseg, mc->base);
	}
}

uintmem
pcixcfgspace(int b)
{
	Mcfg *m;
	int i;

	for(i = 0; i < nmcfg; i++){
		m = &mcfg[i];
		if(m->start <= b && b <= m->end)
			return m->base;
	}
	return 0;
}

enum {
	Blegacy		= 1<<0,
	B8042kbd	= 1<<1,
	Bnovga		= 1<<2,
	Bnomsi		= 1<<3,
	Bnocmos	= 1<<4,
};

static void
iapcbootarch(void)
{
	int i;

	i = fadt.iapcbootarch;

	ioconf.nolegacyprobe = !(i&Blegacy);
	ioconf.noi8042kbd = !(i&B8042kbd);
	ioconf.novga = i&Bnovga;
	ioconf.nomsi = i&Bnomsi;
	ioconf.nocmos = i&Bnocmos;
}

typedef struct Ptab Ptab;
struct Ptab {
	char	*sig;
	void	(*parse)(Tbl*, Parsedat*);
	int	required;
};

static Ptab ptab[] = {
	"APIC",		parseapic,	1,
	"SRAT",		parsesrat,	0,
	"FACP",		parsefadt,	0,
	"HPET",		parsehpet,	0,
	"MCFG",		parsemcfg,	0,
};

static void
parsetables(Parsedat *dat)
{
	int i;
	Tbl *t;
	Ptab *p;

	print("acpi parse: ");
	for(i = 0; i < nelem(ptab); i++){
		p = ptab + i;
		if((t = acpigettbl(p->sig)) != nil){
			p->parse(t, dat);
			print("%s ", p->sig);
		}else if(p->required)
			panic("acpi: parsetables: no %s table\n", p->sig);
	}
	print("\n");
}

int
checkpnpid(void *dot, char *pnpid)
{
	char *id;
	void *p, *x;

	if(dot == nil)
		return -1;
	id = nil;
	if((x = amlwalk(dot, "_HID")) != nil)
		if((p = amlval(x)) != nil)
			id = eisaid(p);
	if(id == nil)
		return -1;
	return strcmp(id, pnpid);
}

enum {
	Present	= 1<<0,
	Decode	= 1<<1,
	Funct	= 1<<3,
	Staok	= Present | Funct,
};

int
staok(void *dot)
{
	int b;
	void *p, *x;

	if((x = amlwalk(dot, "_STA")) == nil)
		return 0;
	if(amleval(x, "", &p) != 0)
		return 0;
	b = amlint(p);
	return b & Staok;
}

static int
evalini(void *dot)
{
	int b;
	void *p, *x;

	b = staok(dot);
	if((b & Present) == 0)
		return -1;
	if((x = amlwalk(dot, "_INI")) != nil){
		if(amleval(x, "", &p) == 0)
			print("eval _INI → %V\n", p);
	}
	return 0;
}

int
cfgpwerb(void)
{
	void *dot;

	dot = amlwalk(amlroot, "\\_SB_.PWRB");
	if(checkpnpid(dot, "PNP0C0C") != 0)
		return 0;
//	print("PWRB %V\n", dot);
	if(evalini(dot) == -1){
		print("PWRB evalini fails\n");
		return 0;
	}
	return 1;
}

static void
cfgsleep(void)
{
	char buf[16];
	uint32_t *t, i;
	void **v;

	/* default soft off values */
	t = acpicfg.sval[5];
	t[0] = 7;
	t[1] = 0;

	/* look for the proper ones */
	for(i = 0; i < 6; i++){
		t = acpicfg.sval[i];
		snprint(buf, sizeof buf, "\\_S%d_", i);
		v = amlval(amlwalk(amlroot, buf));
		if(v != nil && amltag(v) == 'p' && amllen(v) == 4){
			t[0] = amlint(v[0]);
			t[1] = amlint(v[1]);
		}
	}
}

void
acpiinit(int maxmach)
{
	int i;
	Parsedat dat;

	print("acpiinit\n");
	fmtinstall('G', Gfmt);
	maptables();
	amlinit();
	loadtbls("DSDT", 0);
	loadtbls("SSDT", 1);

	memset(&dat, 0, sizeof dat);
	dat.maxmach = maxmach;
	parsetables(&dat);
	if(fadt.smicmd != 0)
		iapcbootarch();

	cfgpwerb();
	cfgsleep();

	/* free the AML interpreter */
	amlexit();

	addarchfile("acpifadt", 0444, readfadt, nil);		/* hack */
	addarchfile("acpitbls", 0444, readtbls, nil);
	if(1||DBGFLG){
		print("acpi load: ");
		for(i = 0; i < ntblmap; i++)
			print("%.4s ", (char*)tblmap[i]->sig);
		print("\n");
	}
	lapicdump();
	iordtdump();
}

int
corecolor(int _1)
{
	return 0;
}
