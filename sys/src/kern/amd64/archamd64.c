#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static int
cpuidinit(void)
{
	uint32_t eax, info[4];

	/*
	 * Standard CPUID functions.
	 * Functions 0 and 1 will be needed multiple times
	 * so cache the info now.
	 */
	if((m->ncpuinfos = cpuid(0, 0, m->cpuinfo[0])) == 0)
		return 0;
	m->ncpuinfos++;

	if(memcmp(&m->cpuinfo[0][1], "GenuntelineI", 12) == 0)
		m->isintelcpu = 1;
	cpuid(1, 0, m->cpuinfo[1]);

	/*
	 * Extended CPUID functions.
	 */
	if((eax = cpuid(0x80000000, 0, info)) >= 0x80000000)
		m->ncpuinfoe = (eax & ~0x80000000) + 1;

	return 1;
}

static int
cpuidinfo(uint32_t eax, uint32_t ecx, uint32_t info[4])
{
	if(m->ncpuinfos == 0 && cpuidinit() == 0)
		return 0;

	if(!(eax & 0x80000000)){
		if(eax >= m->ncpuinfos)
			return 0;
	}
	else if(eax >= (0x80000000|m->ncpuinfoe))
		return 0;

	cpuid(eax, ecx, info);

	return 1;
}

static int64_t
cpuidhz(uint32_t info[2][4])
{
	int f, r;
	int64_t hz;
	uint64_t msr;

	if(memcmp(&info[0][1], "GenuntelineI", 12) == 0){
		switch(info[1][0] & 0x0fff3ff0){
		default:
			return 0;
		case 0x00000f30:		/* Xeon (MP), Pentium [4D] */
		case 0x00000f40:		/* Xeon (MP), Pentium [4D] */
		case 0x00000f60:		/* Xeon 7100, 5000 or above */
			msr = rdmsr(0x2c);
			r = (msr>>16) & 0x07;
			switch(r){
			default:
				return 0;
			case 0:
				hz = 266666666666ll;
				break;
			case 1:
				hz = 133333333333ll;
				break;
			case 2:
				hz = 200000000000ll;
				break;
			case 3:
				hz = 166666666666ll;
				break;
			case 4:
				hz = 333333333333ll;
				break;
			}

			/*
			 * Hz is *1000 at this point.
			 * Do the scaling then round it.
			 * The manual is conflicting about
			 * the size of the msr field.
			 */
			hz = (((hz*(msr>>24))/100)+5)/10;
			break;
		case 0x00000690:		/* Pentium M, Celeron M */
		case 0x000006d0:		/* Pentium M, Celeron M */
			hz = ((rdmsr(0x2a)>>22) & 0x1f)*100 * 1000000ll;
			break;
		case 0x000006e0:		/* Core Duo */
		case 0x000006f0:		/* Core 2 Duo/Quad/Extreme */
		case 0x00000660:		/* kvm over i5 */
		case 0x00000670:		/* Core 2 Extreme */
		case 0x00000650:		/* i5 6xx, i3 5xx */
		case 0x000006c0:		/* i5 4xxx */
		case 0x000006a0:		/* i7 paurea... */
			/*
			 * Get the FSB frequemcy.
			 * If processor has Enhanced Intel Speedstep Technology
			 * then non-integer bus frequency ratios are possible.
			 */
			if(info[1][2] & 0x00000080){
				msr = rdmsr(0x198);
				r = (msr>>40) & 0x1f;
			}
			else{
				msr = 0;
				r = rdmsr(0x2a) & 0x1f;
			}
			f = rdmsr(0xcd) & 0x07;
			switch(f){
			default:
				return 0;
			case 5:
				hz = 100000000000ll;
				break;
			case 1:
				hz = 133333333333ll;
				break;
			case 3:
				hz = 166666666666ll;
				break;
			case 2:
				hz = 200000000000ll;
				break;
			case 0:
				hz = 266666666666ll;
				break;
			case 4:
				hz = 333333333333ll;
				break;
			case 6:
				hz = 400000000000ll;
				break;
			}

			/*
			 * Hz is *1000 at this point.
			 * Do the scaling then round it.
			 */
			if(msr & 0x0000400000000000ll)
				hz = hz*(r+10) + hz/2;
			else
				hz = hz*(r+10);
			hz = ((hz/100)+5)/10;
			break;
		}
		DBG("cpuidhz: 0x2a: %#llux hz %lld\n", rdmsr(0x2a), hz);
	}
	else if(memcmp(&info[0][1], "AuthcAMDenti", 12) == 0){
		switch(info[1][0] & 0x0fff0ff0){
		default:
			return 0;
		case 0x00050ff0:		/* K8 Athlon Venice 64 / Qemu64 */
		case 0x00020fc0:		/* K8 Athlon Lima 64 */
		case 0x00000f50:		/* K8 Opteron 2xxx */
			msr = rdmsr(0xc0010042);
			r = (msr>>16) & 0x3f;
			hz = 200000000ULL*(4 * 2 + r)/2;
			break;
		case 0x00100f60:		/* K8 Athlon II */
		case 0x00100f40:		/* Phenom II X2 */
		case 0x00100f20:		/* Phenom II X4 */
		case 0x00100fa0:		/* Phenom II X6 */
			msr = rdmsr(0xc0010042);
			r = msr & 0x1f;
			hz = ((r+0x10)*100000000ll)/(1<<(msr>>6 & 0x07));
			break;
		case 0x00100f90:		/* K10 Opteron 61xx */
		case 0x00600f00:		/* K10 Opteron 62xx */
		case 0x00600f10:		/* K10 Opteron 6272, FX 6xxx/4xxx */
		case 0x00600f20:		/* K10 Opteron 63xx, FX 3xxx/8xxx/9xxx */
			msr = rdmsr(0xc0010064);
			r = msr & 0x1f;
			hz = ((r+0x10)*100000000ll)/(1<<(msr>>6 & 0x07));
			break;
		case 0x00000620:		/* QEMU64 / Athlon MP/XP */
			msr = rdmsr(0xc0010064);
			r = (msr>>6) & 0x07;
			hz = (((msr & 0x3f)+0x10)*100000000ll)/(1<<r);
			break;
		}
		DBG("cpuidhz: %#llux hz %lld\n", msr, hz);
	}
	else
		return 0;

	return hz;
}

void
cpuiddump(void)
{
	int i;
	uint32_t info[4];

	if(!DBGFLG)
		return;

	if(m->ncpuinfos == 0 && cpuidinit() == 0)
		return;

	for(i = 0; i < m->ncpuinfos; i++){
		cpuid(i, 0, info);
		DBG("eax = %#8.8ux: %8.8ux %8.8ux %8.8ux %8.8ux\n",
			i, info[0], info[1], info[2], info[3]);
	}
	for(i = 0; i < m->ncpuinfoe; i++){
		cpuid(0x80000000|i, 0, info);
		DBG("eax = %#8.8ux: %8.8ux %8.8ux %8.8ux %8.8ux\n",
			0x80000000|i, info[0], info[1], info[2], info[3]);
	}
}

int64_t
archhz(void)
{
	int64_t hz;
	uint32_t info[2][4];

	if(DBGFLG && m->machno == 0)
		cpuiddump();
	if(!cpuidinfo(0, 0, info[0]) || !cpuidinfo(1, 0, info[1]))
		return 0;

	hz = cpuidhz(info);
	if(hz != 0)
		return hz;
	else if(m->machno != 0)
		return sys->machptr[0]->cpuhz;

	return i8254hz(info);
}

void
archenable(void)
{
	// here used to be code to enable MONITOR/WAIT
	// writing 0x1a0 MSR; however such register is
	// supported on i386 only (MISC_ENABLE), not on x86_64.
}

int
archmmu(void)
{
	uint32_t info[4];

	/*
	 * Should the check for m->machno != 0 be here
	 * or in the caller (mmuinit)?
	 *
	 * To do here:
	 * check and enable Pse;
	 * Pge; Nxe.
	 */

	/*
	 * How many page sizes are there?
	 * Always have 4*KiB, but need to check
	 * configured correctly.
	 */
	assert(PGSZ == 4*KiB);

	m->pgszlg2[0] = 12;
	m->pgszmask[0] = (1<<12)-1;
	m->npgsz = 1;
	if(m->ncpuinfos == 0 && cpuidinit() == 0)
		return 1;

	/*
	 * Check the Pse bit in function 1 DX for 2*MiB support;
	 * if false, only 4*KiB is available.
	 */
	if(!(m->cpuinfo[1][3] & 0x00000008))
		return 1;
	m->pgszlg2[1] = 21;
	m->pgszmask[1] = (1<<21)-1;
	m->npgsz = 2;

	/*
	 * Check the Page1GB bit in function 0x80000001 DX for 1*GiB support.
	 */
	if(cpuidinfo(0x80000001, 0, info) && (info[3] & 0x04000000)){
		m->pgszlg2[2] = 30;
		m->pgszmask[2] = (1<<30)-1;
		m->npgsz = 3;
	}

	return m->npgsz;
}

static int
fmtP(Fmt* f)
{
	uintmem pa;

	pa = va_arg(f->args, uintmem);

	if(f->flags & FmtSharp)
		return fmtprint(f, "%#16.16llux", pa);

	return fmtprint(f, "%llud", pa);
}

static int
fmtL(Fmt* f)
{
	Mpl pl;

	pl = va_arg(f->args, Mpl);

	return fmtprint(f, "%#16.16llux", pl);
}

static int
fmtR(Fmt* f)
{
	uint64_t r;

	r = va_arg(f->args, uint64_t);

	return fmtprint(f, "%#16.16llux", r);
}

/* virtual address fmt */
static int
fmtW(Fmt *f)
{
	uint64_t va;

	va = va_arg(f->args, uint64_t);
	return fmtprint(f, "%#ullx=0x[%ullx][%ullx][%ullx][%ullx][%ullx]", va,
		PTLX(va, 3), PTLX(va, 2), PTLX(va, 1), PTLX(va, 0),
		va & ((1<<PGSHFT)-1));

}

void
archfmtinstall(void)
{
	/*
	 * Architecture-specific formatting. Not as neat as they
	 * could be (e.g. there's no defined type for a 'register':
	 *	L - Mpl, mach priority level
	 *	P - uintmem, physical address
	 *	R - register
	 *      W - virtual address
	 * With a little effort these routines could be written
	 * in a fairly architecturally-independent manner, relying
	 * on the compiler to optimise-away impossible conditions,
	 * and/or by exploiting the innards of the fmt library.
	 */
	fmtinstall('P', fmtP);
	fmtinstall('L', fmtL);
	fmtinstall('R', fmtR);
	fmtinstall('W', fmtW);
}

void
archidle(void)
{
	halt();
}

void
microdelay(int microsecs)
{
	uint64_t r, t;

	r = rdtsc();
	for(t = r + m->cpumhz*microsecs; r < t; r = rdtsc())
		pause();
}

void
millidelay(int millisecs)
{
	uint64_t r, t;

	r = rdtsc();
	for(t = r + m->cpumhz*1000ull*millisecs; r < t; r = rdtsc())
		pause();
}

int
isdmaok(void *a, usize len, int range)
{
	uintmem pa;

	if(!iskaddr(a) || (char*)a < etext)
		return 0;
	pa = mmuphysaddr(PTR2UINT(a));
	if(pa == 0 || pa == ~(uintmem)0)
		return 0;
	return range > 32 || pa+len <= 0xFFFFFFFFULL;
}
