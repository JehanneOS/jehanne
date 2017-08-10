#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

#include "mp.h"

extern void initialize_processor(void);

static void
squidboy(Apic* apic)
{
	initialize_processor();
	mmuinit();
	cpuidentify();
	cpuidprint();
	syncclock();
	active.machs[m->machno] = 1;
	apic->online = 1;
	m->online = 1;
	lapicinit(apic);
	lapiconline();
	timersinit();
	schedinit();
}

void
mpstartap(Apic* apic)
{
	uintptr_t *apbootp, *pml4, *pdp0;
	Segdesc *gdt;
	Mach *mach;
	uint8_t *p;
	int i;

	/*
	 * Initialise the AP page-tables and Mach structure.
	 * Xspanalloc will panic if an allocation can't be made.
	 */
	p = xspanalloc(2*PTSZ + BY2PG + MACHSIZE, BY2PG, 0);
	pml4 = (uintptr*)p;
	p += PTSZ;
	pdp0 = (uintptr*)p;
	p += PTSZ;
	gdt = (Segdesc*)p;
	p += BY2PG;
	mach = (Mach*)p;

	memset(pml4, 0, PTSZ);
	memset(pdp0, 0, PTSZ);
	memset(gdt, 0, BY2PG);
	memset(mach, 0, MACHSIZE);

	mach->machno = apic->machno;
	mach->pml4 = pml4;
	mach->gdt  = gdt;	/* filled by mmuinit */
	MACHP(mach->machno) = mach;

	/*
	 * map KZERO (note that we share the KZERO (and VMAP)
	 * PDP between processors.
	 */
	pml4[PTLX(KZERO, 3)] = MACHP(0)->pml4[PTLX(KZERO, 3)];
	pml4[PTLX(VMAP, 3)] = MACHP(0)->pml4[PTLX(VMAP, 3)];

	/* double map */
	pml4[0] = PADDR(pdp0) | PTEWRITE|PTEVALID;
	pdp0[0] = *mmuwalk(pml4, KZERO, 2, 0);

	/*
	 * Tell the AP where its kernel vector and pdb are.
	 * The offsets are known in the AP bootstrap code.
	 */
	apbootp = (uintptr*)(APBOOTSTRAP+0x08);
	apbootp[0] = (uintptr)squidboy;	/* assembler jumps here eventually */
	apbootp[1] = (uintptr)PADDR(pml4);
	apbootp[2] = (uintptr)apic;
	apbootp[3] = (uintptr)mach;

	/*
	 * Universal Startup Algorithm.
	 */
	p = KADDR(0x467);		/* warm-reset vector */
	*p++ = PADDR(APBOOTSTRAP);
	*p++ = PADDR(APBOOTSTRAP)>>8;
	i = (PADDR(APBOOTSTRAP) & ~0xFFFF)/16;
	/* code assumes i==0 */
	if(i != 0)
		print("mp: bad APBOOTSTRAP\n");
	*p++ = i;
	*p = i>>8;
	coherence();

	nvramwrite(0x0F, 0x0A);		/* shutdown code: warm reset upon init ipi */
	lapicstartap(apic, PADDR(APBOOTSTRAP));
	for(i = 0; i < 100000; i++){
		if(arch->fastclock == tscticks)
			cycles(&m->tscticks);	/* for ap's syncclock(); */
		if(apic->online)
			break;
		delay(1);
	}
	nvramwrite(0x0F, 0x00);
}
