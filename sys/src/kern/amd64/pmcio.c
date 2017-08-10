/*
 *  Performance counters non portable part
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"amd64.h"
#include	"pmc.h"

typedef struct PmcCfg PmcCfg;
typedef struct PmcCore PmcCore;

enum {
	PeUnk,
	PeAmd,
	/*
	 *	See Vol 3B Intel
	 *	64 Architecture's Software Developer's manual
	 */
	PeIntel,
};

enum {
	_PeUnk,
	/* Non architectural */
	PeIntelSandy,
	PeIntelNehalem,
	PeIntelWestmere,
	/*
	 * See  BKDG for AMD cfg.family 10 Processors
	 * section 2.16 and 3.14
	*/
	PeK10,

};

enum {
	PeNregAmd	= 4,	/* Number of Pe/Pct regs for K10 */
};

enum {						/* MSRs */
	PerfCtrbaseIntel= 0x000000c1,		/* Performance Counters */
	PerfEvtbaseIntel= 0x00000186,		/* Performance Event Select */
	PerfGlobalCtr	= 0x0000038f,		/* Performance Event Global Ctrl, intel */

	PerfEvtbaseAmd	= 0xc0010000,		/* Performance Event Select */
	PerfCtrbaseAmd	= 0xc0010004,		/* Performance Counters */
};

enum {						/* HW Performance Counters Event Selector */

	PeHo		= 0x0000020000000000ull,/* Host only */
	PeGo		= 0x0000010000000000ull,/* Guest only */
	PeEvMskH	= 0x0000000f00000000ull,/* Event mask H */
	PeCtMsk		= 0x00000000ff000000ull,/* Counter mask */
	PeInMsk		= 0x0000000000800000ull,/* Invert mask */
	PeCtEna		= 0x0000000000400000ull,/* Counter enable */
	PeInEna		= 0x0000000000100000ull,/* Interrupt enable */
	PePnCtl		= 0x0000000000080000ull,/* Pin control */
	PeEdg		= 0x0000000000040000ull,/* Edge detect */
	PeOS		= 0x0000000000020000ull,/* OS mode */
	PeUsr		= 0x0000000000010000ull,/* User mode */
	PeUnMsk		= 0x000000000000ff00ull,/* Unit Mask */
	PeEvMskL	= 0x00000000000000ffull,/* Event Mask L */

	PeEvMsksh	= 32ull,		/* Event mask shift */
};

struct PmcCfg {
	int nregs;
	uint32_t ctrbase;
	uint32_t evtbase;
	int vendor;
	int family;
	PmcCtlCtrId *pmcidsarch;
	PmcCtlCtrId *pmcids;
};

extern int pmcdebug;

static PmcCfg cfg;
static PmcCore pmccore[MACHMAX];

static void pmcmachupdate(void);

int
pmcnregs(void)
{
	uint32_t info[4];
	int nregs;

	if(cfg.nregs != 0)
		return cfg.nregs;	/* don't call cpuid more than necessary */
	switch(cfg.vendor){
	case PeAmd:
		nregs = PeNregAmd;
		break;
	case PeIntel:
		cpuid(0xa, info);
		nregs = (info[0]>>8)&0xff;
		break;
	default:
		nregs = 0;
	}
	if(nregs > PmcMaxCtrs)
		nregs = PmcMaxCtrs;
	return nregs;
}

static uint64_t
pmcmsk(void)
{
	uint32_t info[4];
	uint64_t msk;

	msk = 0;
	switch(cfg.vendor){
	case PeAmd:
		msk = ~0ULL;
		break;
	case PeIntel:
		cpuid(0xa, info);
		msk = (1<<((info[0]>>16)&0xff)) - 1;
		break;
	}
	return msk;
}

PmcCtlCtrId pmcidsk10[] = {
	{"locked instr", "0x024 0x1"},
	{"locked cycles nonspecul", "0x024 0x4"},	/* in  cycles */
	{"SMI intr", "0x02b 0x0"},
	{"DC access", "0x040 0x0"},
	{"DC miss", "0x041 0x0"},
	{"DC refills", "0x042 0x1f"},
	{"DC evicted", "0x042 0x3f"},
	{"L1 DTLB miss", "0x045 0x7"},				/* DTLB L2 hits */
	{"L2 DTLB miss", "0x046 0x7"},
	{"L1 DTLB hit", "0x04d 0x3"},
	{"global TLB flush", "0x054 0x0"},
	{"L2 hit", "0x07d 0x3f"},
	{"L2 miss", "0x07e 0xf"},
	{"IC miss", "0x081 0x0"},
	{"IC refill from L2", "0x082 0x0"},
	{"IC refill from system", "0x083 0x0"},
	{"L1 ITLB miss", "0x084 0x0"},					/* L2 ITLB hits */
	{"L2 ITLB miss", "0x085 0x3"},
	{"DRAM access", "0x0e0 0x3f"},
	//{"L3 miss core 0", "0x4e1 0x13"},
	//{"L3 miss core 1", "0x4e1 0x23"},
	//{"L3 miss core 2", "0x4e1 0x43"},
	//{"L3 miss core 3", "0x4e1 0x83"},
	{"L3 miss", "0x4e1 0xf3"},						/* all cores in the socket */
	{"", ""},
};

/*18.2.3 Intel Software Deveveloper's Manual */
PmcCtlCtrId pmcidsintel[] = {
	{"unhalted cycles", "0x3c 0x0"},
	{"instr", "0xc0 0x0"},
	{"Llast misses", "0x2e 0x41"},
	{"branch instr", "0xc4 0x0"},
	{"branch misses", "0xc5 0x0 "},
	{"", ""},
};

/* Table 19.7 Intel Software Deveveloper's Manual */
PmcCtlCtrId pmcidsandy[] = {
	{"DTLB walk cycles", "0x49 0x4"},				/* all levels */
	{"DTLB miss", "0x8 0x2"},
	{"DTLB hit", "0x8 0x4"},
	{"L2 hit", "0x24 0x4"},
	{"L2 miss", "0x24 0x8"},
	{"IL2 hit", "0x24 0x10"},
	{"IL2 miss", "0x24 0x20"},
	{"ITLB miss", "0x85 0x2"},
	{"ITLB walk cycles", "0x85 0x4"},
	{"ITLB flush", "0xae 0x1"},
	{"mem loads", "0xd0 0xf1"},					/* counts μops */
	{"mem stores", "0xd0 0xf2"},
	{"mem ops", "0xd0 0xf3"},
	{"", ""},
};

#define X86MODEL(x)	((((x)>>4) & 0x0F) | (((x)>>16) & 0x0F)<<4)
#define X86FAMILY(x)	((((x)>>8) & 0x0F) | (((x)>>20) & 0xFF)<<4)

static int
pmcintelfamily(void)
{
	uint32_t info, fam, mod;

	info = m->cpuinfo[1][0];

	fam = X86FAMILY(info);
	mod = X86MODEL(info);
	if(fam != 0x6)
		return PeUnk;
	switch(mod){
	case 0x2a:
		return PeIntelSandy;
	case 0x1a:
	case 0x1e:
	case 0x1f:
		return PeIntelNehalem;
	case 0x25:
	case 0x2c:
		return PeIntelWestmere;
	}
	return PeUnk;
}

void
pmcinitctl(PmcCtl *p)
{
	jehanne_memset(p, 0xff, sizeof(PmcCtl));
	p->enab = PmcCtlNullval;
	p->user = PmcCtlNullval;
	p->os = PmcCtlNullval;
	p->nodesc = 1;
}

void
pmcconfigure(void)
{
	Mach *mach;
	int i, j, isrecog;

	isrecog = 0;

	if(jehanne_memcmp(&m->cpuinfo[0][1], "AuthcAMDenti", 12) == 0){
		isrecog++;
		cfg.ctrbase = PerfCtrbaseAmd;
		cfg.evtbase = PerfEvtbaseAmd;
		cfg.vendor = PeAmd;
		cfg.family = PeUnk;
		cfg.pmcidsarch = pmcidsk10;
	}else if(jehanne_memcmp(&m->cpuinfo[0][1], "GenuntelineI", 12) == 0){
		isrecog++;
		cfg.ctrbase = PerfCtrbaseIntel;
		cfg.evtbase = PerfEvtbaseIntel;
		cfg.vendor = PeIntel;
		cfg.family = pmcintelfamily();
		cfg.pmcidsarch = pmcidsintel;
		switch(cfg.family){
		case PeIntelSandy:
			cfg.pmcids = pmcidsandy;
			break;
		case PeIntelNehalem:
		case PeIntelWestmere:
			break;
		}
	}else
		cfg.vendor = PeUnk;

	cfg.nregs = pmcnregs();
	if(isrecog)
		pmcupdate = pmcmachupdate;

	for(i = 0; i < MACHMAX; i++) {
		if((mach = sys->machptr[i]) != nil && mach->online != 0){
			for(j = 0; j < cfg.nregs; j++)
				pmcinitctl(&pmccore[i].ctr[j]);
		}
	}
}

static void
pmcenab(void)
{
	switch(cfg.vendor){
	case PeAmd:
		return;
	case PeIntel:
		wrmsr(PerfGlobalCtr, pmcmsk());
		break;
	}
}

/* so they can be read from user space */
static int
pmcuserenab(int enable)
{
	uint64_t cr4;

	cr4 = cr4get();
	if (enable){
		cr4 |= Pce;
	} else
		cr4 &=  ~Pce;
	cr4put(cr4);
	return cr4&Pce;
}

int
pmctrans(PmcCtl *p)
{
	PmcCtlCtrId *pi;
	int n;

	n = 0;
	if(cfg.pmcidsarch != nil)
		for (pi = &cfg.pmcidsarch[0]; pi->portdesc[0] != '\0'; pi++){
			if (jehanne_strncmp(p->descstr, pi->portdesc, jehanne_strlen(pi->portdesc)) == 0){
				jehanne_strncpy(p->descstr, pi->archdesc, jehanne_strlen(pi->archdesc) + 1);
				n = 1;
				break;
			}
		}
	/* this ones supersede the other ones */
	if(cfg.pmcids != nil)
		for (pi = &cfg.pmcids[0]; pi->portdesc[0] != '\0'; pi++){
			if (jehanne_strncmp(p->descstr, pi->portdesc, jehanne_strlen(pi->portdesc)) == 0){
				jehanne_strncpy(p->descstr, pi->archdesc, jehanne_strlen(pi->archdesc) + 1);
				n = 1;
				break;
			}
		}
	if(pmcdebug != 0)
		jehanne_print("really setting %s\n", p->descstr);
	return n;
}

//PeHo|PeGo
#define PeAll	(PeOS|PeUsr)
#define SetEvMsk(v, e) ((v)|(((e)&PeEvMskL)|(((e)<<(PeEvMsksh-8))&PeEvMskH)))
#define SetUMsk(v, u) ((v)|(((u)<<8ull)&PeUnMsk))

#define GetEvMsk(e) (((e)&PeEvMskL)|(((e)&PeEvMskH)>>(PeEvMsksh-8)))
#define GetUMsk(u) (((u)&PeUnMsk)>>8ull)

static int
getctl(PmcCtl *p, uint32_t regno)
{
	uint64_t r, e, u;

	r = rdmsr(regno + cfg.evtbase);
	p->enab = (r&PeCtEna) != 0;
	p->user = (r&PeUsr) != 0;
	p->os = (r&PeOS) != 0;
	e = GetEvMsk(r);
	u = GetUMsk(r);
	/* TODO inverse translation */
	jehanne_snprint(p->descstr, KNAMELEN, "%#ullx %#ullx", e, u);
	p->nodesc = 0;
	return 0;
}

static int
pmcanyenab(void)
{
	int i;
	PmcCtl p;

	for (i = 0; i < cfg.nregs; i++) {
		if (getctl(&p, i) < 0)
			return -1;
		if (p.enab)
			return 1;
	}

	return 0;
}


static int
setctl(PmcCtl *p, int regno)
{
	uint64_t v, e, u;
	char *toks[2];
	char str[KNAMELEN];

	v = rdmsr(regno + cfg.evtbase);
	v &= PeEvMskH|PeEvMskL|PeCtEna|PeOS|PeUsr|PeUnMsk;
	if (p->enab != PmcCtlNullval)
		if (p->enab)
			v |= PeCtEna;
		else
			v &= ~PeCtEna;

	if (p->user != PmcCtlNullval)
		if (p->user)
			v |= PeUsr;
		else
			v &= ~PeUsr;

	if (p->os != PmcCtlNullval)
		if (p->os)
			v |= PeOS;
		else
			v &= ~PeOS;

	if (pmctrans(p) < 0)
		return -1;

	if (p->nodesc == 0) {
		jehanne_memmove(str, p->descstr, KNAMELEN);
		if (jehanne_tokenize(str, toks, 2) != 2)
			return -1;
		e = jehanne_atoi(toks[0]);
		u = jehanne_atoi(toks[1]);
		v &= ~(PeEvMskL|PeEvMskH|PeUnMsk);
		v |= SetEvMsk(v, e);
		v |= SetUMsk(v, u);
	}
	wrmsr(regno+ cfg.evtbase, v);
	pmcuserenab(pmcanyenab());
	if (pmcdebug) {
		v = rdmsr(regno+ cfg.evtbase);
		jehanne_print("conf pmc[%#ux]: %#llux\n", regno, v);
	}
	return 0;
}

int
pmcdescstr(char *str, int nstr)
{
	PmcCtlCtrId *pi;
	int ns;

	ns = 0;

	if(pmcdebug != 0)
		jehanne_print("vendor %x family %x nregs %d pmcnregs %d\n", cfg.vendor, cfg.family, cfg.nregs, pmcnregs());
	if(cfg.pmcidsarch == nil && cfg.pmcids == nil){
		*str = 0;
		return ns;
	}

	if(cfg.pmcidsarch != nil)
		for (pi = &cfg.pmcidsarch[0]; pi->portdesc[0] != '\0'; pi++)
			ns += jehanne_snprint(str + ns, nstr - ns, "%s\n",pi->portdesc);
	if(cfg.pmcids != nil)
		for (pi = &cfg.pmcids[0]; pi->portdesc[0] != '\0'; pi++)
			ns += jehanne_snprint(str + ns, nstr - ns, "%s\n",pi->portdesc);
	return ns;
}

static uint64_t
getctr(uint32_t regno)
{
	return rdmsr(regno + cfg.ctrbase);
}

static int
setctr(uint64_t v, uint32_t regno)
{
	wrmsr(regno + cfg.ctrbase, v);
	return 0;
}

uint64_t
pmcgetctr(uint32_t coreno, uint32_t regno)
{
	PmcCtr *p;
	uint64_t ctr;

	if (regno >= cfg.nregs)
		error("invalid reg");
	p = &pmccore[coreno].ctr[regno];

	ilock(&pmccore[coreno]);
	if(coreno == m->machno)
		ctr = getctr(regno);
	else
		ctr = p->ctr;
	iunlock(&pmccore[coreno]);

	return ctr;
}

int
pmcsetctr(uint32_t coreno, uint64_t v, uint32_t regno)
{
	PmcCtr *p;
	int n;

	if (regno >= cfg.nregs)
		error("invalid reg");
	p = &pmccore[coreno].ctr[regno];

	ilock(&pmccore[coreno]);
	if(coreno == m->machno)
		n = setctr(v, regno);
	else{
		p->ctr = v;
		p->ctrset |= PmcSet;
		p->stale = 1;
		n = 0;
	}
	iunlock(&pmccore[coreno]);

	return n;
}

static void
ctl2ctl(PmcCtl *dctl, PmcCtl *sctl)
{
	if(sctl->enab != PmcCtlNullval)
		dctl->enab = sctl->enab;
	if(sctl->user != PmcCtlNullval)
		dctl->user = sctl->user;
	if(sctl->os != PmcCtlNullval)
		dctl->os = sctl->os;
	if(sctl->nodesc == 0) {
		jehanne_memmove(dctl->descstr, sctl->descstr, KNAMELEN);
		dctl->nodesc = 0;
	}
}

int
pmcsetctl(uint32_t coreno, PmcCtl *pctl, uint32_t regno)
{
	PmcCtr *p;
	int n;

	if (regno >= cfg.nregs)
		error("invalid reg");
	p = &pmccore[coreno].ctr[regno];

	ilock(&pmccore[coreno]);
	if(coreno == m->machno)
		n = setctl(pctl, regno);
	else{
		ctl2ctl(&p->PmcCtl, pctl);
		p->ctlset |= PmcSet;
		p->stale = 1;
		n = 0;
	}
	iunlock(&pmccore[coreno]);

	return n;
}

int
pmcgetctl(uint32_t coreno, PmcCtl *pctl, uint32_t regno)
{
	PmcCtr *p;
	int n;

	if (regno >= cfg.nregs)
		error("invalid reg");
	p = &pmccore[coreno].ctr[regno];

	ilock(&pmccore[coreno]);
	if(coreno == m->machno)
		n = getctl(pctl, regno);
	else{
		jehanne_memmove(pctl, &p->PmcCtl, sizeof(PmcCtl));
		n = 0;
	}
	iunlock(&pmccore[coreno]);

	return n;
}

static void
pmcmachupdate(void)
{
	PmcCtr *p;
	int coreno, i, maxct;

	if((maxct = cfg.nregs) <= 0)
		return;
	coreno = m->machno;

	ilock(&pmccore[coreno]);
	for (i = 0; i < maxct; i++) {
		p = &pmccore[coreno].ctr[i];
		if(p->ctrset & PmcSet)
			setctr(p->ctr, i);
		if(p->ctlset & PmcSet)
			setctl(p, i);
		p->ctr = getctr(i);
		getctl(p, i);
		p->ctrset = PmcIgn;
		p->ctlset = PmcIgn;
		p->stale = 0;
	}
	iunlock(&pmccore[coreno]);
}
