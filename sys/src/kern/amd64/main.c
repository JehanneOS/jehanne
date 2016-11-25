/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "io.h"
#include "apic.h"

extern void confoptions(void);	/* XXX - must go */
extern void confsetenv(void);	/* XXX - must go */

static uintptr_t sp;		/* XXX - must go - user stack of init proc */

Sys* sys = (Sys*)0x1;		/* dummy value to put it in data section
				 * - entry.S set it correctly
				 * - main memset to zero the bss section
				 */
usize sizeofSys = sizeof(Sys);

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 * Optionsinit() is called from multiboot() to
 * set it all up.
 */
static int64_t oargc;
static char* oargv[20];
static char oargb[1024];
static int oargblen;

static int maxcores = 1024;	/* max # of cores given as an argument */
static int numtcs = 32;		/* initial # of TCs */
IOConf	ioconf;
int	procmax;

char *cputype = "amd64";
char dbgflg[256];
static int vflag = 1;

void
optionsinit(char* s)
{
	oargblen = strecpy(oargb, oargb+sizeof(oargb), s) - oargb;
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

static void
options(int argc, char* argv[])
{
	char *p;
	char *env[2];
	int n, o;
	char envcopy[256];

	/*
	 * Process flags.
	 * Flags [A-Za-z] may be optionally followed by
	 * an integer level between 1 and 127 inclusive
	 * (no space between flag and level).
	 * '--' ends flag processing.
	 */
	while(--argc > 0){
		char* next = *++argv;
		if(next[0] =='-' && next[1] != '-'){
			while(o = *++argv[0]){
				if(!(o >= 'A' && o <= 'Z') && !(o >= 'a' && o <= 'z'))
					continue;
				n = strtol(argv[0]+1, &p, 0);
				if(p == argv[0]+1 || n < 1 || n > 127)
					n = 1;
				argv[0] = p-1;
				dbgflg[o] = n;
			}
		}else if(strcmp(next, "waitgdb") == 0){
			waitdebugger();
		}else if(strcmp(next, "idlespin") == 0){
			onIdleSpin();
		}else{
			strncpy(envcopy, next, sizeof envcopy);
			gettokens(envcopy, env, 2, "=");
			if(strcmp(env[0], "maxcores") == 0){
				maxcores = strtol(env[1], 0, 0);
			}
			if(strcmp(env[0], "numtcs") == 0){
				numtcs = strtol(env[1], 0, 0);
			}
		}
	}
	vflag = dbgflg['v'];
}

void
loadenv(int argc, char* argv[])
{
	char *env[2];

	/*
	 * Process command line env options
	 */
	while(--argc > 0){
		char* next = *++argv;
		if(next[0] !='-'){
			if (gettokens(next, env, 2, "=")  == 2){;
				ksetenv(env[0], env[1], 1);
			}else{
				print("Ignoring parameter with no value: %s\n", env[0]);
			}
		}
	}
}

void
squidboy(int apicno)
{
	int64_t hz;

	sys->machptr[m->machno] = m;

	/*
	 * Need something for initial delays
	 * until a timebase is worked out.
	 */
	m->cpuhz = sys->machptr[0]->cpuhz;
	m->cyclefreq = m->cpuhz;
	m->cpumhz = sys->machptr[0]->cpumhz;
	m->perf.period = 1;

	DBG("Hello Squidboy %d %d\n", apicno, m->machno);

	vsvminit(MACHSTKSZ);

	/*
	 * Beware the Curse of The Non-Interruptable Were-Temporary.
	 */
	hz = archhz();
	if(hz == 0)
		ndnr();
	m->cpuhz = hz;
	m->cpumhz = hz/1000000ll;

	archenable();

	mmuinit();
	if(!lapiconline())
		ndnr();

	fpuinit();

	/*
	 * Handshake with sipi to let it
	 * know the Startup IPI succeeded.
	 */
	m->splpc = 0;

	/*
	 * Handshake with main to proceed with initialisation.
	 */
	while(sys->epoch == 0)
		;
	wrmsr(0x10, sys->epoch);
	m->rdtsc = rdtsc();

	DBG("mach %d is go %#p %#p %3p\n", m->machno, m, m->pml4->pte, &apicno);

	timersinit();

	/*
	 * Cannot allow interrupts while waiting for online.
	 * However, by taking the lowering of the APIC task priority
	 * out of apiconline something could be done here with
	 * MONITOR/MWAIT perhaps to drop the energy used by the
	 * idle core.
	 */
	while(!m->online)
		pause();
	lapictimerenable();
	lapicpri(0);

	print("mach%d: online color %d\n", m->machno, m->color);
	schedinit();

	panic("squidboy returns (type %d)", m->mode);
}

#define	D(c)	if(0)outb(0x3f8, (c))

void
main(uint32_t ax, uint32_t bx)
{
	int i;
	int64_t hz;
	char *p;

	memset(edata, 0, end - edata);

	/*
	 * ilock via i8250enable via i8250console
	 * needs m->machno, sys->machptr[] set, and
	 * also 'up' set to nil.
	 */
	cgapost(sizeof(uintptr_t)*8);
	memset(m, 0, sizeof(Mach));
	m->machno = 0;
	m->online = 1;
	sys->machptr[m->machno] = &sys->mach;
	m->stack = PTR2UINT(sys->machstk);
	m->vsvm = sys->vsvmpage;
	sys->nmach = 1;
	sys->nonline = 1;
	sys->copymode = 0;	/* copy on write */
	up = nil;

	confoptions();
	asminit();
	multiboot(ax, bx, 0);
	options(oargc, oargv);
	p = getconf("*dbflags");
	if(p != nil){
		for(; *p != 0; p++)
			if(*p >= 'a' && *p <= 'z' || *p >= 'A' && *p <= 'Z')
				dbgflg[(uint8_t)*p] = 1;
	}

	/*
	 * Need something for initial delays
	 * until a timebase is worked out.
	 */
	m->cpuhz = 2000000000ll;
	m->cpumhz = 2000;

	cgainit();
	i8250console("0");
	consputs = cgaconsputs;

	vsvminit(MACHSTKSZ);

	active.exiting = 0;

	fmtinit();
	print("\nJehanne Operating System\n");

	if(vflag){
		print("&ax = %#p, ax = %#ux, bx = %#ux\n", &ax, ax, bx);
		multiboot(ax, bx, vflag);
	}
	e820();

	m->perf.period = 1;
	if((hz = archhz()) != 0ll){
		m->cpuhz = hz;
		m->cpumhz = hz/1000000ll;
	}

	archenable();

	/*
	 * Mmuinit before meminit because it
	 * makes mappings and
	 * flushes the TLB via m->pml4->pa.
	 */
	mmuinit();

	ioinit();
	keybinit();

	meminit();
	archinit();
	physallocinit();
D('a');
	mallocinit();
D('b');
	memdebug();
	trapinit();
D('c');

	/*
	 * Printinit will cause the first malloc
	 * call to happen (printinit->qopen->malloc).
	 * If the system dies here it's probably due
	 * to malloc not being initialised
	 * correctly, or the data segment is misaligned
	 * (it's amazing how far you can get with
	 * things like that completely broken).
	 */
	printinit();
D('d');
	/*
	 * This is necessary with GRUB and QEMU.
	 * Without it an interrupt can occur at a weird vector,
	 * because the vector base is likely different, causing
	 * havoc. Do it before any APIC initialisation.
	 */
	i8259init(IdtPIC);
D('e');

	acpiinit(MACHMAX);
D('f');
//	mpsinit();
D('g');
	lapiconline();
	ioapiconline();
D('h');
	intrenable(IdtTIMER, timerintr, 0, -1, "APIC timer");
	lapictimerenable();
	lapicpri(0);
D('i');

	timersinit();
D('j');
	keybenable();
	mouseenable();
D('k');
	fpuinit();
D('l');
	p = getconf("*procmax");
	if(p != nil)
		procmax = strtoull(p, nil, 0);
	if(procmax == 0)
		procmax = 2000;
	psinit(procmax);
D('m');
//	imagepool_init();
D('n');
	links();
D('o');
	devtabreset();
D('p');
	umem_init();
D('r');

	userinit();
D('s');
	if(!dbgflg['S'])
		sipi();
D('t');

	sys->epoch = rdtsc();
	wrmsr(0x10, sys->epoch);
	m->rdtsc = rdtsc();

D('u');
	/*
	 * Release the hounds.
	 */
	for(i = 1; i < MACHMAX; i++){
		if(sys->machptr[i] == nil)
			continue;

		ainc(&sys->nonline);

		sys->machptr[i]->color = corecolor(i);
		if(sys->machptr[i]->color < 0)
			sys->machptr[i]->color = 0;
		sys->machptr[i]->online = 1;
	}
D('v');
prflush();
	schedinit();
}

void
init0(void)
{
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

//	if(consuart == nil)
//		i8250console("0");
	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	devtabinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "AMD64", conffile);
		loadenv(oargc, oargv);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "amd64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		confsetenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	kproc("awake", awakekproc, 0);
	touser(sp);
}

void
bootargs(uintptr_t base)
{
	int i;
	uint32_t ssize;
	char **av, *p;

	/*
	 * Push the boot args onto the stack.
	 * Make sure the validaddr check in syscall won't fail
	 * because there are fewer than the maximum number of
	 * args by subtracting sizeof(up->arg).
	 */
	i = oargblen+1;
	p = UINT2PTR(STACKALIGN(base + PGSZ - sizeof(up->arg) - i));
	memmove(p, oargb, i);

	/*
	 * Now push argc and the argv pointers.
	 * This isn't strictly correct as the code jumped to by
	 * touser in init9.[cs] calls startboot (port/initcode.c) which
	 * expects arguments
	 * 	startboot(char* argv0, char* argv[])
	 * not the usual (int argc, char* argv[]), but argv0 is
	 * unused so it doesn't matter (at the moment...).
	 */
	av = (char**)(p - (oargc+2)*sizeof(char*));
	ssize = base + PGSZ - PTR2UINT(av);
	*av++ = (char*)oargc;
	for(i = 0; i < oargc; i++)
		*av++ = (oargv[i] - oargb) + (p - base) + (USTKTOP - PGSZ);
	*av = nil;

	sp = USTKTOP - ssize;
}

void
userinit(void)
{
	Proc *p;
	ProcSegment *s;
	PagePointer page;
	char *k;
	uintptr_t va, mmuphys;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->r.ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);
	/*
	 * Kernel Stack
	 *
	 * N.B. make sure there's enough space for syscall to check
	 *	for valid args and
	 *	space for gotolabel's return PC
	 * AMD64 stack must be quad-aligned.
	 */
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->arg)-sizeof(uintptr_t));
	p->sched.sp = STACKALIGN(p->sched.sp);
D('0');
	/*
	 * User Stack
	 */
	s = nil;
	if(!segment_virtual(&s, SgStack, SgRead|SgWrite, 0, USTKTOP-USTKSIZE, USTKTOP))
		panic("userinit: cannot create stack segment");
D('1');
	p->seg[SSEG] = s;
	va = USTKTOP-USTKSIZE;
	mmuphys = 0;
	if(!segment_fault(&mmuphys, &va, s, FaultWrite))
		panic("userinit: cannot allocate first stack page");
D('2');
	page = segment_page(s, va);
	if(page == 0)
		panic("userinit: cannot find first stack page (previously faulted)");
	k = page_kmap(page);
	bootargs(PTR2UINT(k));
	page_kunmap(page, &k);

	/*
	 * Text
	 */
	s = nil;
	if(!segment_userinit(&s, 0))
		panic("userinit: cannot create text segment");
	p->seg[TSEG] = s;

	/*
	 * Data
	 */
	s = nil;
	if(!segment_userinit(&s, 1))
		panic("userinit: cannot create data segment");
	p->seg[DSEG] = s;

	ready(p);
}

static void
fullstop(void)
{
	splhi();
	lapicpri(0xff);
	/* i8259 was initialised as disabled */
	for(;;)
		_halt();
}

static void
shutdown(int ispanic)
{
	int ms;

	if(!m->online)
		fullstop();

	active.ispanic = ispanic;
	m->online = 0;
	active.exiting = 1;
	adec(&sys->nonline);

	iprint("cpu%d: exiting\n", m->machno);
	/* wait for any other processors to shutdown */
	//spllo();
	prflush();
	for(ms = 10*1000; ms > 0; ms -= 2){
		delay(2);
		if(sys->nonline == 0 && consactive() == 0)
			break;
	}

	if(active.ispanic){
		if(!cpuserver || getconf("*debug") || 1)
			fullstop();
		delay(10000);
	}
	else
		delay(1000);
}

void
reboot(void* _1, void* _2, long _3)
{
	panic("reboot\n");
}

void
exit(int ispanic)
{
	shutdown(ispanic);
	archreset();
}
