/*
 * PCI support code.
 * Needs a massive rewrite.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum
{					/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

					/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */

	MaxFNO		= 7,
	MaxUBN		= 255,
};

enum
{					/* command register */
	IOen		= (1<<0),
	MEMen		= (1<<1),
	MASen		= (1<<2),
	MemWrInv	= (1<<4),
	PErrEn		= (1<<6),
	SErrEn		= (1<<8),
};

static Lock pcicfglock;
static Lock pcicfginitlock;
static int pcicfgmode = -1;
static int pcimaxbno = 255;
static int pcimaxdno;
static Pcidev* pciroot;
static Pcidev* pcilist;
static Pcidev* pcitail;
static int nobios, nopcirouting;
static BIOS32si* pcibiossi;

static int pcicfgrw8raw(int, int, int, int);
static int pcicfgrw16raw(int, int, int, int);
static int pcicfgrw32raw(int, int, int, int);

static int (*pcicfgrw8)(int, int, int, int) = pcicfgrw8raw;
static int (*pcicfgrw16)(int, int, int, int) = pcicfgrw16raw;
static int (*pcicfgrw32)(int, int, int, int) = pcicfgrw32raw;

static char* bustypes[] = {
	"CBUSI",
	"CBUSII",
	"EISA",
	"FUTURE",
	"INTERN",
	"ISA",
	"MBI",
	"MBII",
	"MCA",
	"MPI",
	"MPSA",
	"NUBUS",
	"PCI",
	"PCMCIA",
	"TC",
	"VL",
	"VME",
	"XPRESS",
};

static int
tbdffmt(Fmt* fmt)
{
	char *p;
	int l, r;
	uint32_t type, tbdf;

	if((p = jehanne_malloc(READSTR)) == nil)
		return jehanne_fmtstrcpy(fmt, "(tbdfconv)");

	switch(fmt->r){
	case 'T':
		tbdf = va_arg(fmt->args, int);
		if(tbdf == BUSUNKNOWN)
			jehanne_snprint(p, READSTR, "unknown");
		else{
			type = BUSTYPE(tbdf);
			if(type < nelem(bustypes))
				l = jehanne_snprint(p, READSTR, bustypes[type]);
			else
				l = jehanne_snprint(p, READSTR, "%d", type);
			jehanne_snprint(p+l, READSTR-l, ".%d.%d.%d",
				BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
		}
		break;

	default:
		jehanne_snprint(p, READSTR, "(tbdfconv)");
		break;
	}
	r = jehanne_fmtstrcpy(fmt, p);
	jehanne_free(p);

	return r;
}

uint32_t
pcibarsize(Pcidev *p, int rno)
{
	uint32_t v, size;

	v = pcicfgrw32(p->tbdf, rno, 0, 1);
	pcicfgrw32(p->tbdf, rno, 0xFFFFFFF0, 0);
	size = pcicfgrw32(p->tbdf, rno, 0, 1);
	if(v & 1)
		size |= 0xFFFF0000;
	pcicfgrw32(p->tbdf, rno, v, 0);

	return -(size & ~0x0F);
}

static int
pcisizcmp(void *a, void *b)
{
	Pcisiz *aa, *bb;

	aa = a;
	bb = b;
	return aa->siz - bb->siz;
}

static uint32_t
pcimask(uint32_t v)
{
	uint32_t m;

	m = BI2BY*sizeof(v);
	for(m = 1<<(m-1); m != 0; m >>= 1) {
		if(m & v)
			break;
	}

	m--;
	if((v & m) == 0)
		return v;

	v |= m;
	return v+1;
}

static void
pcibusmap(Pcidev *root, uint32_t *pmema, uint32_t *pioa, int wrreg)
{
	Pcidev *p;
	int ntb, i, size, rno, hole;
	uint32_t v, mema, ioa, sioa, smema, base, limit;
	Pcisiz *table, *tptr, *mtb, *itb;

	if(!nobios)
		return;

	ioa = *pioa;
	mema = *pmema;

	DBG("pcibusmap wr=%d %T mem=%luX io=%luX\n",
		wrreg, root->tbdf, mema, ioa);

	ntb = 0;
	for(p = root; p != nil; p = p->link)
		ntb++;

	ntb *= (PciCIS-PciBAR0)/4;
	table = jehanne_malloc(2*ntb*sizeof(Pcisiz));
	if(table == nil)
		panic("pcibusmap: can't allocate memory");
	itb = table;
	mtb = table+ntb;

	/*
	 * Build a table of sizes
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->ccrb == 0x06) {
			if(p->ccru != 0x04 || p->bridge == nil) {
				DBG("pci: ignored bridge %T\n", p->tbdf);
				continue;
			}

			sioa = ioa;
			smema = mema;
			pcibusmap(p->bridge, &smema, &sioa, 0);

			hole = pcimask(smema-mema);
			if(hole < (1<<20))
				hole = 1<<20;
			p->mema.size = hole;

			hole = pcimask(sioa-ioa);
			if(hole < (1<<12))
				hole = 1<<12;

			p->ioa.size = hole;

			itb->dev = p;
			itb->bar = -1;
			itb->siz = p->ioa.size;
			itb++;

			mtb->dev = p;
			mtb->bar = -1;
			mtb->siz = p->mema.size;
			mtb++;
			continue;
		}

		for(i = 0; i <= 5; i++) {
			rno = PciBAR0 + i*4;
			v = pcicfgrw32(p->tbdf, rno, 0, 1);
			size = pcibarsize(p, rno);
			if(size == 0)
				continue;

			p->mem[i].size = size;
			if(v & 1) {
				itb->dev = p;
				itb->bar = i;
				itb->siz = size;
				itb++;
			}
			else {
				mtb->dev = p;
				mtb->bar = i;
				mtb->siz = size;
				mtb++;

				if((v & 7) == 4)
					i++;
			}
		}
	}

	/*
	 * Sort both tables IO smallest first, Memory largest
	 */
	jehanne_qsort(table, itb-table, sizeof(Pcisiz), pcisizcmp);
	tptr = table+ntb;
	jehanne_qsort(tptr, mtb-tptr, sizeof(Pcisiz), pcisizcmp);

	/*
	 * Allocate IO address space on this bus
	 */
	for(tptr = table; tptr < itb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<12;
		ioa = (ioa+hole-1) & ~(hole-1);

		p = tptr->dev;
		if(tptr->bar == -1)
			p->ioa.bar = ioa;
		else {
			p->pcr |= IOen;
			p->mem[tptr->bar].bar = ioa|1;
			if(wrreg)
				pcicfgrw32(p->tbdf, PciBAR0+(tptr->bar*4), ioa|1, 0);
		}

		ioa += tptr->siz;
	}

	/*
	 * Allocate Memory address space on this bus
	 */
	for(tptr = table+ntb; tptr < mtb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<20;
		mema = (mema+hole-1) & ~(hole-1);

		p = tptr->dev;
		if(tptr->bar == -1)
			p->mema.bar = mema;
		else {
			p->pcr |= MEMen;
			p->mem[tptr->bar].bar = mema;
			if(wrreg)
				pcicfgrw32(p->tbdf, PciBAR0+(tptr->bar*4), mema, 0);
		}
		mema += tptr->siz;
	}

	*pmema = mema;
	*pioa = ioa;
	jehanne_free(table);

	if(wrreg == 0)
		return;

	/*
	 * Finally set all the bridge addresses & registers
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->bridge == nil) {
			pcicfgrw8(p->tbdf, PciLTR, 64, 0);

			p->pcr |= MASen;
			pcicfgrw16(p->tbdf, PciPCR, p->pcr, 0);
			continue;
		}

		base = p->ioa.bar;
		limit = base+p->ioa.size-1;
		v = pcicfgrw32(p->tbdf, PciIBR, 0, 1);
		v = (v&0xFFFF0000)|(limit & 0xF000)|((base & 0xF000)>>8);
		pcicfgrw32(p->tbdf, PciIBR, v, 0);
		v = (limit & 0xFFFF0000)|(base>>16);
		pcicfgrw32(p->tbdf, PciIUBR, v, 0);

		base = p->mema.bar;
		limit = base+p->mema.size-1;
		v = (limit & 0xFFF00000)|((base & 0xFFF00000)>>16);
		pcicfgrw32(p->tbdf, PciMBR, v, 0);

		/*
		 * Disable memory prefetch
		 */
		pcicfgrw32(p->tbdf, PciPMBR, 0x0000FFFF, 0);
		pcicfgrw8(p->tbdf, PciLTR, 64, 0);

		/*
		 * Enable the bridge
		 */
		p->pcr |= IOen|MEMen|MASen;
		pcicfgrw32(p->tbdf, PciPCR, 0xFFFF0000|p->pcr , 0);

		sioa = p->ioa.bar;
		smema = p->mema.bar;
		pcibusmap(p->bridge, &smema, &sioa, 1);
	}
}

static int
pcilscan(int bno, Pcidev** list, Pcidev *parent)
{
	Pcidev *p, *head, *tail;
	int dno, fno, i, hdt, l, maxfno, maxubn, rno, sbn, tbdf, ubn;

	maxubn = bno;
	head = nil;
	tail = nil;
	for(dno = 0; dno <= pcimaxdno; dno++){
		maxfno = 0;
		for(fno = 0; fno <= maxfno; fno++){
			/*
			 * For this possible device, form the
			 * bus+device+function triplet needed to address it
			 * and try to read the vendor and device ID.
			 * If successful, allocate a device struct and
			 * start to fill it in with some useful information
			 * from the device's configuration space.
			 */
			tbdf = MKBUS(BusPCI, bno, dno, fno);
			l = pcicfgrw32(tbdf, PciVID, 0, 1);
			if(l == 0xFFFFFFFF || l == 0)
				continue;
			p = jehanne_malloc(sizeof(*p));
			if(p == nil)
				panic("pcilscan: can't allocate memory");
			p->tbdf = tbdf;
			p->vid = l;
			p->did = l>>16;

			if(pcilist != nil)
				pcitail->list = p;
			else
				pcilist = p;
			pcitail = p;

			p->pcr = pcicfgr16(p, PciPCR);
			p->rid = pcicfgr8(p, PciRID);
			p->ccrp = pcicfgr8(p, PciCCRp);
			p->ccru = pcicfgr8(p, PciCCRu);
			p->ccrb = pcicfgr8(p, PciCCRb);
			p->cls = pcicfgr8(p, PciCLS);
			p->ltr = pcicfgr8(p, PciLTR);

			p->intl = pcicfgr8(p, PciINTL);

			/*
			 * If the device is a multi-function device adjust the
			 * loop count so all possible functions are checked.
			 */
			hdt = pcicfgr8(p, PciHDT);
			if(hdt & 0x80)
				maxfno = MaxFNO;

			/*
			 * If appropriate, read the base address registers
			 * and work out the sizes.
			 */
			switch(p->ccrb) {
			case 0x00:		/* prehistoric */
			case 0x01:		/* mass storage controller */
			case 0x02:		/* network controller */
			case 0x03:		/* display controller */
			case 0x04:		/* multimedia device */
			case 0x07:		/* simple comm. controllers */
			case 0x08:		/* base system peripherals */
			case 0x09:		/* input devices */
			case 0x0A:		/* docking stations */
			case 0x0B:		/* processors */
			case 0x0C:		/* serial bus controllers */
			case 0x0D:		/* wireless controllers */
			case 0x0E:		/* intelligent I/O controllers */
			case 0x0F:		/* sattelite communication controllers */
			case 0x10:		/* encryption/decryption controllers */
			case 0x11:		/* signal processing controllers */
				if((hdt & 0x7F) != 0)
					break;
				rno = PciBAR0;
				for(i = 0; i <= 5; i++) {
					p->mem[i].bar = pcicfgr32(p, rno);
					p->mem[i].size = pcibarsize(p, rno);
					if((p->mem[i].bar & 7) == 4 && i < 5){
						uint32_t hi;

						rno += 4;
						hi = pcicfgr32(p, rno);
						if(hi != 0){
							jehanne_print("ignoring 64-bit bar %d: %llux %d from %T\n",
								i, (uint64_t)hi<<32 | p->mem[i].bar, p->mem[i].size, p->tbdf);
							p->mem[i].bar = 0;
							p->mem[i].size = 0;
						}
						i++;
					}
					rno += 4;
				}
				break;

			case 0x05:		/* memory controller */
			case 0x06:		/* bridge device */
			default:
				break;
			}

			p->parent = parent;
			if(head != nil)
				tail->link = p;
			else
				head = p;
			tail = p;
		}
	}

	*list = head;
	for(p = head; p != nil; p = p->link){
		/*
		 * Find PCI-PCI bridges and recursively descend the tree.
		 */
		if(p->ccrb != 0x06 || p->ccru != 0x04)
			continue;

		/*
		 * If the secondary or subordinate bus number is not
		 * initialised try to do what the PCI BIOS should have
		 * done and fill in the numbers as the tree is descended.
		 * On the way down the subordinate bus number is set to
		 * the maximum as it's not known how many buses are behind
		 * this one; the final value is set on the way back up.
		 */
		sbn = pcicfgr8(p, PciSBN);
		ubn = pcicfgr8(p, PciUBN);

		if(sbn == 0 || ubn == 0 || nobios) {
			sbn = maxubn+1;
			/*
			 * Make sure memory, I/O and master enables are
			 * off, set the primary, secondary and subordinate
			 * bus numbers and clear the secondary status before
			 * attempting to scan the secondary bus.
			 *
			 * Initialisation of the bridge should be done here.
			 */
			pcicfgw32(p, PciPCR, 0xFFFF0000);
			l = (MaxUBN<<16)|(sbn<<8)|bno;
			pcicfgw32(p, PciPBN, l);
			pcicfgw16(p, PciSPSR, 0xFFFF);
			maxubn = pcilscan(sbn, &p->bridge, p);
			l = (maxubn<<16)|(sbn<<8)|bno;

			pcicfgw32(p, PciPBN, l);
		}
		else {
			if(ubn > maxubn)
				maxubn = ubn;
			pcilscan(sbn, &p->bridge, p);
		}
	}

	return maxubn;
}

int
pciscan(int bno, Pcidev **list)
{
	int ubn;

	lock(&pcicfginitlock);
	ubn = pcilscan(bno, list, nil);
	unlock(&pcicfginitlock);
	return ubn;
}

static uint8_t
pIIxget(Pcidev *router, uint8_t link)
{
	uint8_t pirq;

	/* link should be 0x60, 0x61, 0x62, 0x63 */
	pirq = pcicfgr8(router, link);
	return (pirq < 16)? pirq: 0;
}

static void
pIIxset(Pcidev *router, uint8_t link, uint8_t irq)
{
	pcicfgw8(router, link, irq);
}

static uint8_t
viaget(Pcidev *router, uint8_t link)
{
	uint8_t pirq;

	/* link should be 1, 2, 3, 5 */
	pirq = (link < 6)? pcicfgr8(router, 0x55 + (link>>1)): 0;

	return (link & 1)? (pirq >> 4): (pirq & 15);
}

static void
viaset(Pcidev *router, uint8_t link, uint8_t irq)
{
	uint8_t pirq;

	pirq = pcicfgr8(router, 0x55 + (link >> 1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x55 + (link>>1), pirq);
}

static uint8_t
optiget(Pcidev *router, uint8_t link)
{
	uint8_t pirq = 0;

	/* link should be 0x02, 0x12, 0x22, 0x32 */
	if ((link & 0xcf) == 0x02)
		pirq = pcicfgr8(router, 0xb8 + (link >> 5));
	return (link & 0x10)? (pirq >> 4): (pirq & 15);
}

static void
optiset(Pcidev *router, uint8_t link, uint8_t irq)
{
	uint8_t pirq;

	pirq = pcicfgr8(router, 0xb8 + (link >> 5));
    	pirq &= (link & 0x10)? 0x0f : 0xf0;
    	pirq |= (link & 0x10)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0xb8 + (link >> 5), pirq);
}

static uint8_t
aliget(Pcidev *router, uint8_t link)
{
	/* No, you're not dreaming */
	static const uint8_t map[] = { 0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15 };
	uint8_t pirq;

	/* link should be 0x01..0x08 */
	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	return (link & 1)? map[pirq&15]: map[pirq>>4];
}

static void
aliset(Pcidev *router, uint8_t link, uint8_t irq)
{
	/* Inverse of map in aliget */
	static const uint8_t map[] = { 0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15 };
	uint8_t pirq;

	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (map[irq] << 4): (map[irq] & 15);
	pcicfgw8(router, 0x48 + ((link-1)>>1), pirq);
}

static uint8_t
cyrixget(Pcidev *router, uint8_t link)
{
	uint8_t pirq;

	/* link should be 1, 2, 3, 4 */
	pirq = pcicfgr8(router, 0x5c + ((link-1)>>1));
	return ((link & 1)? pirq >> 4: pirq & 15);
}

static void
cyrixset(Pcidev *router, uint8_t link, uint8_t irq)
{
	uint8_t pirq;

	pirq = pcicfgr8(router, 0x5c + (link>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x5c + (link>>1), pirq);
}

typedef struct Bridge Bridge;
struct Bridge
{
	uint16_t	vid;
	uint16_t	did;
	uint8_t	(*get)(Pcidev *, uint8_t);
	void	(*set)(Pcidev *, uint8_t, uint8_t);
};

static Bridge southbridges[] = {
	{ 0x8086, 0x122e, pIIxget, pIIxset },	/* Intel 82371FB */
	{ 0x8086, 0x1234, pIIxget, pIIxset },	/* Intel 82371MX */
	{ 0x8086, 0x7000, pIIxget, pIIxset },	/* Intel 82371SB */
	{ 0x8086, 0x7110, pIIxget, pIIxset },	/* Intel 82371AB */
	{ 0x8086, 0x7198, pIIxget, pIIxset },	/* Intel 82443MX (fn 1) */
	{ 0x8086, 0x2410, pIIxget, pIIxset },	/* Intel 82801AA */
	{ 0x8086, 0x2420, pIIxget, pIIxset },	/* Intel 82801AB */
	{ 0x8086, 0x2440, pIIxget, pIIxset },	/* Intel 82801BA */
	{ 0x8086, 0x2448, pIIxget, pIIxset },	/* Intel 82801BAM/CAM/DBM */
	{ 0x8086, 0x244c, pIIxget, pIIxset },	/* Intel 82801BAM */
	{ 0x8086, 0x244e, pIIxget, pIIxset },	/* Intel 82801 */
	{ 0x8086, 0x2480, pIIxget, pIIxset },	/* Intel 82801CA */
	{ 0x8086, 0x248c, pIIxget, pIIxset },	/* Intel 82801CAM */
	{ 0x8086, 0x24c0, pIIxget, pIIxset },	/* Intel 82801DBL */
	{ 0x8086, 0x24cc, pIIxget, pIIxset },	/* Intel 82801DBM */
	{ 0x8086, 0x24d0, pIIxget, pIIxset },	/* Intel 82801EB */
	{ 0x8086, 0x25a1, pIIxget, pIIxset },	/* Intel 6300ESB */
	{ 0x8086, 0x2640, pIIxget, pIIxset },	/* Intel 82801FB */
	{ 0x8086, 0x2641, pIIxget, pIIxset },	/* Intel 82801FBM */
	{ 0x8086, 0x2670, pIIxget, pIIxset },	/* Intel 632xesb */
	{ 0x8086, 0x27b8, pIIxget, pIIxset },	/* Intel 82801GB */
	{ 0x8086, 0x27b9, pIIxget, pIIxset },	/* Intel 82801GBM */
	{ 0x8086, 0x27bd, pIIxget, pIIxset },	/* Intel 82801GB/GR */
	{ 0x8086, 0x3a16, pIIxget, pIIxset },	/* Intel 82801JIR */
	{ 0x8086, 0x3a40, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x3a42, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x3a48, pIIxget, pIIxset },	/* Intel 82801JI */
	{ 0x8086, 0x2916, pIIxget, pIIxset },	/* Intel 82801? */
	{ 0x8086, 0x1c02, pIIxget, pIIxset },	/* Intel 6 Series/C200 */
	{ 0x8086, 0x1e53, pIIxget, pIIxset },	/* Intel 7 Series/C216 */
	{ 0x8086, 0x8c56, pIIxget, pIIxset },	/* Intel 8 Series/C226 */
	{ 0x8086, 0x2810, pIIxget, pIIxset },	/* Intel 82801HB/HR (ich8/r) */
	{ 0x8086, 0x2812, pIIxget, pIIxset },	/* Intel 82801HH (ich8dh) */
	{ 0x8086, 0x2912, pIIxget, pIIxset },	/* Intel 82801ih ich9dh */
	{ 0x8086, 0x2914, pIIxget, pIIxset },	/* Intel 82801io ich9do */
	{ 0x8086, 0x2916, pIIxget, pIIxset },	/* Intel 82801ibr ich9r */
	{ 0x8086, 0x2917, pIIxget, pIIxset },	/* Intel 82801iem ich9m-e  */
	{ 0x8086, 0x2918, pIIxget, pIIxset },	/* Intel 82801ib ich9 */
	{ 0x8086, 0x2919, pIIxget, pIIxset },	/* Intel 82801? ich9m  */
	{ 0x8086, 0x3a16, pIIxget, pIIxset },	/* Intel 82801jir ich10r */
	{ 0x8086, 0x3a18, pIIxget, pIIxset },	/* Intel 82801jib ich10 */
	{ 0x8086, 0x3a40, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a42, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a48, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3b06, pIIxget, pIIxset },	/* Intel 82801? ibex peak */
	{ 0x8086, 0x3b14, pIIxget, pIIxset },	/* Intel 82801? 3420 */
	{ 0x8086, 0x1c49, pIIxget, pIIxset },	/* Intel 82hm65 cougar point pch */
	{ 0x8086, 0x1c4b, pIIxget, pIIxset },	/* Intel 82hm67 */
	{ 0x8086, 0x1c4f, pIIxget, pIIxset },	/* Intel 82qm67 cougar point pch */
	{ 0x8086, 0x1c52, pIIxget, pIIxset },	/* Intel 82q65 cougar point pch */
	{ 0x8086, 0x1c54, pIIxget, pIIxset },	/* Intel 82q67 cougar point pch */
	{ 0x8086, 0x1e55, pIIxget, pIIxset },	/* Intel QM77 panter point lpc */

	{ 0x1106, 0x0586, viaget, viaset },	/* Viatech 82C586 */
	{ 0x1106, 0x0596, viaget, viaset },	/* Viatech 82C596 */
	{ 0x1106, 0x0686, viaget, viaset },	/* Viatech 82C686 */
	{ 0x1106, 0x3177, viaget, viaset },	/* Viatech VT8235 */
	{ 0x1106, 0x3227, viaget, viaset },	/* Viatech VT8237 */
	{ 0x1106, 0x3287, viaget, viaset },	/* Viatech VT8251 */
	{ 0x1106, 0x8410, viaget, viaset },	/* Viatech PV530 bridge */
	{ 0x1045, 0xc700, optiget, optiset },	/* Opti 82C700 */
	{ 0x10b9, 0x1533, aliget, aliset },	/* Al M1533 */
	{ 0x1039, 0x0008, pIIxget, pIIxset },	/* SI 503 */
	{ 0x1039, 0x0496, pIIxget, pIIxset },	/* SI 496 */
	{ 0x1078, 0x0100, cyrixget, cyrixset },	/* Cyrix 5530 Legacy */

	{ 0x1022, 0x746b, nil, nil },		/* AMD 8111 */
	{ 0x10de, 0x00d1, nil, nil },		/* NVIDIA nForce 3 */
	{ 0x10de, 0x00e0, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x10de, 0x00e1, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x1166, 0x0200, nil, nil },		/* ServerWorks ServerSet III LE */
	{ 0x1002, 0x4377, nil, nil },		/* ATI Radeon Xpress 200M */
	{ 0x1002, 0x4372, nil, nil },		/* ATI SB400 */
	{ 0x1002, 0x9601, nil, nil },		/* AMD SB710 */
	{ 0x1002, 0x438d, nil, nil },		/* AMD SB600 */
	{ 0x1002, 0x439d, nil, nil },		/* AMD SB810 */
};

typedef struct Slot Slot;
struct Slot {
	uint8_t	bus;		/* Pci bus number */
	uint8_t	dev;		/* Pci device number */
	uint8_t	maps[12];	/* Avoid structs!  Link and mask. */
	uint8_t	slot;		/* Add-in/built-in slot */
	uint8_t	reserved;
};

typedef struct Router Router;
struct Router {
	uint8_t	signature[4];	/* Routing table signature */
	uint8_t	version[2];	/* Version number */
	uint8_t	size[2];	/* Total table size */
	uint8_t	bus;		/* Interrupt router bus number */
	uint8_t	devfn;		/* Router's devfunc */
	uint8_t	pciirqs[2];	/* Exclusive PCI irqs */
	uint8_t	compat[4];	/* Compatible PCI interrupt router */
	uint8_t	miniport[4];	/* Miniport data */
	uint8_t	reserved[11];
	uint8_t	checksum;
};

static uint16_t pciirqs;		/* Exclusive PCI irqs */
static Bridge *southbridge;	/* Which southbridge to use. */

static void
pcirouting(void)
{
	Slot *e;
	Router *r;
	int i, size, tbdf;
	Pcidev *sbpci, *pci;
	uint8_t *p, pin, irq, link, *map;

	if((p = sigsearch("$PIR")) == nil)
		return;

	r = (Router*)p;
	size = (r->size[1] << 8)|r->size[0];
	if(size < sizeof(Router) || checksum(r, size))
		return;

	if(0) jehanne_print("PCI interrupt routing table version %d.%d at %p\n",
		r->version[0], r->version[1], r);

	tbdf = MKBUS(BusPCI, r->bus, (r->devfn>>3)&0x1f, r->devfn&7);
	sbpci = pcimatchtbdf(tbdf);
	if(sbpci == nil) {
		jehanne_print("pcirouting: Cannot find south bridge %T\n", tbdf);
		return;
	}

	for(i = 0; i < nelem(southbridges); i++)
		if(sbpci->vid == southbridges[i].vid && sbpci->did == southbridges[i].did)
			break;

	if(i == nelem(southbridges)) {
		jehanne_print("pcirouting: ignoring south bridge %T %.4uX/%.4uX\n", tbdf, sbpci->vid, sbpci->did);
		return;
	}
	southbridge = &southbridges[i];
	if(southbridge->get == nil)
		return;

	pciirqs = (r->pciirqs[1] << 8)|r->pciirqs[0];
	for(e = (Slot *)&r[1]; (uint8_t *)e < p + size; e++) {
		if(0) {
			jehanne_print("%.2uX/%.2uX %.2uX: ", e->bus, e->dev, e->slot);
			for (i = 0; i < 4; i++) {
				map = &e->maps[i * 3];
				jehanne_print("[%d] %.2uX %.4uX ", i, map[0], (map[2] << 8)|map[1]);
			}
			jehanne_print("\n");
		}
		for(i = 0; i < 8; i++) {
			tbdf = MKBUS(BusPCI, e->bus, (e->dev>>3)&0x1f, i);
			pci = pcimatchtbdf(tbdf);
			if(pci == nil)
				continue;
			pin = pcicfgr8(pci, PciINTP);
			if(pin == 0 || pin == 0xff)
				continue;

			map = &e->maps[((pin - 1) % 4) * 3];
			link = map[0];
			irq = southbridge->get(sbpci, link);
			if(irq == pci->intl)
				continue;
			if(irq == 0 || (irq & 0x80) != 0){
				irq = pci->intl;
				if(irq == 0 || irq == 0xff)
					continue;
				if(southbridge->set == nil)
					continue;
				southbridge->set(sbpci, link, irq);
			}
			jehanne_print("pcirouting: %T at pin %d link %.2uX irq %d -> %d\n", tbdf, pin, link, pci->intl, irq);
			pcicfgw8(pci, PciINTL, irq);
			pci->intl = irq;
		}
	}
}

static void pcireservemem(void);

static int
pcicfgrw8bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	jehanne_memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB108;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFF;
	}
	else{
		ci.eax = 0xB10B;
		ci.ecx = data & 0xFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw16bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	jehanne_memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB109;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFFFF;
	}
	else{
		ci.eax = 0xB10C;
		ci.ecx = data & 0xFFFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw32bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	if(pcibiossi == nil)
		return -1;

	jehanne_memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB10A;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx;
	}
	else{
		ci.eax = 0xB10D;
		ci.ecx = data;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static BIOS32si*
pcibiosinit(void)
{
	BIOS32ci ci;
	BIOS32si *si;

	if((si = bios32open("$PCI")) == nil)
		return nil;

	jehanne_memset(&ci, 0, sizeof(BIOS32ci));
	ci.eax = 0xB101;
	if(bios32ci(si, &ci) || ci.edx != ((' '<<24)|('I'<<16)|('C'<<8)|'P')){
		jehanne_free(si);
		return nil;
	}
	if(ci.eax & 0x01)
		pcimaxdno = 31;
	else
		pcimaxdno = 15;
	pcimaxbno = ci.ecx & 0xff;

	return si;
}

void
pcibussize(Pcidev *root, uint32_t *msize, uint32_t *iosize)
{
	*msize = 0;
	*iosize = 0;
	pcibusmap(root, msize, iosize, 0);
}

static void
pcicfginit(void)
{
	char *p;
	Pcidev **list;
	uint32_t mema, ioa;
	int bno, n, pcibios;

	lock(&pcicfginitlock);
	if(pcicfgmode != -1)
		goto out;

	pcibios = 0;
	if(getconf("*nobios"))
		nobios = 1;
	else if(getconf("*pcibios"))
		pcibios = 1;
	if(getconf("*nopcirouting"))
		nopcirouting = 1;

	/*
	 * Try to determine which PCI configuration mode is implemented.
	 * Mode2 uses a byte at 0xCF8 and another at 0xCFA; Mode1 uses
	 * a DWORD at 0xCF8 and another at 0xCFC and will pass through
	 * any non-DWORD accesses as normal I/O cycles. There shouldn't be
	 * a device behind these addresses so if Mode1 accesses fail try
	 * for Mode2 (Mode2 is deprecated).
	 */
	if(!pcibios){
		/*
		 * Bits [30:24] of PciADDR must be 0,
		 * according to the spec.
		 */
		n = inl(PciADDR);
		if(!(n & 0x7F000000)){
			outl(PciADDR, 0x80000000);
			outb(PciADDR+3, 0);
			if(inl(PciADDR) & 0x80000000){
				pcicfgmode = 1;
				pcimaxdno = 31;
			}
		}
		outl(PciADDR, n);

		if(pcicfgmode < 0){
			/*
			 * The 'key' part of PciCSE should be 0.
			 */
			n = inb(PciCSE);
			if(!(n & 0xF0)){
				outb(PciCSE, 0x0E);
				if(inb(PciCSE) == 0x0E){
					pcicfgmode = 2;
					pcimaxdno = 15;
				}
			}
			outb(PciCSE, n);
		}
	}

	if(pcicfgmode < 0 || pcibios) {
		if((pcibiossi = pcibiosinit()) == nil)
			goto out;
		pcicfgrw8 = pcicfgrw8bios;
		pcicfgrw16 = pcicfgrw16bios;
		pcicfgrw32 = pcicfgrw32bios;
		pcicfgmode = 3;
	}

	jehanne_fmtinstall('T', tbdffmt);

	if(p = getconf("*pcimaxbno"))
		pcimaxbno = jehanne_strtoul(p, 0, 0);
	if(p = getconf("*pcimaxdno")){
		n = jehanne_strtoul(p, 0, 0);
		if(n < pcimaxdno)
			pcimaxdno = n;
	}

	list = &pciroot;
	for(bno = 0; bno <= pcimaxbno; bno++) {
		int sbno = bno;
		bno = pcilscan(bno, list, nil);

		while(*list)
			list = &(*list)->link;

		if (sbno == 0) {
			Pcidev *pci;

			/*
			  * If we have found a PCI-to-Cardbus bridge, make sure
			  * it has no valid mappings anymore.
			  */
			for(pci = pciroot; pci != nil; pci = pci->link){
				if (pci->ccrb == 6 && pci->ccru == 7) {
					uint16_t bcr;

					/* reset the cardbus */
					bcr = pcicfgr16(pci, PciBCR);
					pcicfgw16(pci, PciBCR, 0x40 | bcr);
					delay(50);
				}
			}
		}
	}

	if(pciroot == nil)
		goto out;

	if(nobios) {
		/*
		 * Work out how big the top bus is
		 */
		pcibussize(pciroot, &mema, &ioa);

		/*
		 * Align the windows and map it
		 */
		ioa = 0x1000;
		mema = 0x90000000;

		DBG("Mask sizes: mem=%lux io=%lux\n", mema, ioa);

		pcibusmap(pciroot, &mema, &ioa, 1);
		DBG("Sizes2: mem=%lux io=%lux\n", mema, ioa);

		goto out;
	}

	if(!nopcirouting)
		pcirouting();

out:
	pcireservemem();
	unlock(&pcicfginitlock);

	if(getconf("*pcihinv"))
		pcihinv(nil);
}

static void
pcireservemem(void)
{
	int i;
	Pcidev *p;

	/*
	 * mark all the physical address space claimed by pci devices
	 * as in use, so that upaalloc doesn't give it out.
	 */
	for(p=pciroot; p; p=p->list)
		for(i=0; i<nelem(p->mem); i++)
			if(p->mem[i].bar && (p->mem[i].bar&1) == 0)
				memreserve(p->mem[i].bar&~0x0F, p->mem[i].size);
}

static int
pcicfgrw8raw(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x03;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inb(PciDATA+o);
		else
			outb(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inb((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outb((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr8(Pcidev* pcidev, int rno)
{
	return pcicfgrw8(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw8(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw8(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw16raw(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x02;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = ins(PciDATA+o);
		else
			outs(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = ins((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outs((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr16(Pcidev* pcidev, int rno)
{
	return pcicfgrw16(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw16(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw16(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw32raw(int tbdf, int rno, int data, int read)
{
	int type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inl(PciDATA);
		else
			outl(PciDATA, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inl((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outl((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr32(Pcidev* pcidev, int rno)
{
	return pcicfgrw32(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw32(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw32(pcidev->tbdf, rno, data, 0);
}

Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(pcicfgmode == -1)
		pcicfginit();

	if(prev == nil)
		prev = pcilist;
	else
		prev = prev->list;

	while(prev != nil){
		if((vid == 0 || prev->vid == vid)
		&& (did == 0 || prev->did == did))
			break;
		prev = prev->list;
	}
	return prev;
}

Pcidev*
pcimatchtbdf(int tbdf)
{
	Pcidev *pcidev;

	if(pcicfgmode == -1)
		pcicfginit();

	for(pcidev = pcilist; pcidev != nil; pcidev = pcidev->list) {
		if(pcidev->tbdf == tbdf)
			break;
	}
	return pcidev;
}

uint8_t
pciipin(Pcidev *pci, uint8_t pin)
{
	if (pci == nil)
		pci = pcilist;

	while (pci) {
		uint8_t intl;

		if (pcicfgr8(pci, PciINTP) == pin && pci->intl != 0 && pci->intl != 0xff)
			return pci->intl;

		if (pci->bridge && (intl = pciipin(pci->bridge, pin)) != 0)
			return intl;

		pci = pci->list;
	}
	return 0;
}

static void
pcilhinv(Pcidev* p)
{
	int i;
	Pcidev *t;

	if(p == nil) {
		p = pciroot;
		jehanne_print("bus dev type vid  did intl memory\n");
	}
	for(t = p; t != nil; t = t->link) {
		jehanne_print("%d  %2d/%d %.2ux %.2ux %.2ux %.4ux %.4ux %3d  ",
			BUSBNO(t->tbdf), BUSDNO(t->tbdf), BUSFNO(t->tbdf),
			t->ccrb, t->ccru, t->ccrp, t->vid, t->did, t->intl);

		for(i = 0; i < nelem(p->mem); i++) {
			if(t->mem[i].size == 0)
				continue;
			jehanne_print("%d:%.8lux %d ", i,
				t->mem[i].bar, t->mem[i].size);
		}
		if(t->ioa.bar || t->ioa.size)
			jehanne_print("ioa:%.8lux %d ", t->ioa.bar, t->ioa.size);
		if(t->mema.bar || t->mema.size)
			jehanne_print("mema:%.8lux %d ", t->mema.bar, t->mema.size);
		if(t->bridge)
			jehanne_print("->%d", BUSBNO(t->bridge->tbdf));
		jehanne_print("\n");
	}
	while(p != nil) {
		if(p->bridge != nil)
			pcilhinv(p->bridge);
		p = p->link;
	}
}

void
pcihinv(Pcidev* p)
{
	if(pcicfgmode == -1)
		pcicfginit();
	lock(&pcicfginitlock);
	pcilhinv(p);
	unlock(&pcicfginitlock);
}

void
pcireset(void)
{
	Pcidev *p;

	if(pcicfgmode == -1)
		pcicfginit();

	for(p = pcilist; p != nil; p = p->list) {
		/* don't mess with the bridges */
		if(p->ccrb == 0x06)
			continue;
		pciclrbme(p);
	}
}

void
pcisetioe(Pcidev* p)
{
	p->pcr |= IOen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrioe(Pcidev* p)
{
	p->pcr &= ~IOen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pcisetbme(Pcidev* p)
{
	p->pcr |= MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrbme(Pcidev* p)
{
	p->pcr &= ~MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pcisetmwi(Pcidev* p)
{
	p->pcr |= MemWrInv;
	pcicfgw16(p, PciPCR, p->pcr);
}

void
pciclrmwi(Pcidev* p)
{
	p->pcr &= ~MemWrInv;
	pcicfgw16(p, PciPCR, p->pcr);
}

static int
enumcaps(Pcidev *p, int (*fmatch)(Pcidev*, int, int, int), int arg)
{
	int i, r, cap, off;

	/* status register bit 4 has capabilities */
	if((pcicfgr16(p, PciPSR) & 1<<4) == 0)
		return -1;      
	switch(pcicfgr8(p, PciHDT) & 0x7F){
	default:
		return -1;
	case 0:                         /* etc */
	case 1:                         /* pci to pci bridge */
		off = 0x34;
		break;
	case 2:                         /* cardbus bridge */
		off = 0x14;
		break;
	}
	for(i = 48; i--;){
		off = pcicfgr8(p, off);
		if(off < 0x40 || (off & 3))
			break;
		off &= ~3;
		cap = pcicfgr8(p, off);
		if(cap == 0xff)
			break;
		r = (*fmatch)(p, cap, off, arg);
		if(r < 0)
			break;
		if(r == 0)
			return off;
		off++;
	}
	return -1;
}

static int
matchcap(Pcidev* _, int cap, int __, int arg)
{
	return cap != arg;
}

static int
matchhtcap(Pcidev *p, int cap, int off, int arg)
{
	int mask;

	if(cap != PciCapHTC)
		return 1;
	if(arg == 0x00 || arg == 0x20)
		mask = 0xE0;
	else
		mask = 0xF8;
	cap = pcicfgr8(p, off+3);
	return (cap & mask) != arg;
}

int
pcicap(Pcidev *p, int cap)
{
	return enumcaps(p, matchcap, cap);
}

int
pcihtcap(Pcidev *p, int cap)
{
	return enumcaps(p, matchhtcap, cap);
}

static int
pcigetpmrb(Pcidev* p)
{
        if(p->pmrb != 0)
                return p->pmrb;
        return p->pmrb = pcicap(p, PciCapPMG);
}

int
pcigetpms(Pcidev* p)
{
	int pmcsr, ptr;

	if((ptr = pcigetpmrb(p)) == -1)
		return -1;

	/*
	 * Power Management Register Block:
	 *  offset 0:	Capability ID
	 *	   1:	next item pointer
	 *	   2:	capabilities
	 *	   4:	control/status
	 *	   6:	bridge support extensions
	 *	   7:	data
	 */
	pmcsr = pcicfgr16(p, ptr+4);

	return pmcsr & 0x0003;
}

int
pcisetpms(Pcidev* p, int state)
{
	int ostate, pmc, pmcsr, ptr;

	if((ptr = pcigetpmrb(p)) == -1)
		return -1;

	pmc = pcicfgr16(p, ptr+2);
	pmcsr = pcicfgr16(p, ptr+4);
	ostate = pmcsr & 0x0003;
	pmcsr &= ~0x0003;

	switch(state){
	default:
		return -1;
	case 0:
		break;
	case 1:
		if(!(pmc & 0x0200))
			return -1;
		break;
	case 2:
		if(!(pmc & 0x0400))
			return -1;
		break;
	case 3:
		break;
	}
	pmcsr |= state;
	pcicfgw16(p, ptr+4, pmcsr);

	return ostate;
}

int
pcinextcap(Pcidev *pci, int offset)
{
	if(offset == 0) {
		if((pcicfgr16(pci, PciPSR) & (1<<4)) == 0)
			return 0; /* no capabilities */
		offset = PciCP-1;
	}
	return pcicfgr8(pci, offset+1) & ~3;
}
