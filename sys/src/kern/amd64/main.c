/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2017 Giacomo Tesio <giacomo@tesio.it>
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
#include "ureg.h"
#include "pool.h"

#include "io.h"
#include "apic.h"

static uintptr_t sp;		/* XXX - must go - user stack of init proc */

Sys system;
Sys* sys;
usize sizeofSys = sizeof(Sys);

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 * Optionsinit() is called from multiboot() to
 * set it all up.
 */
static int64_t oargc;
static char* oargv[64];
static char oargb[1024];
static int oargblen;

static int maxcores = 1024;	/* max # of cores given as an argument */
static int numtcs = 32;		/* initial # of TCs */
IOConf	ioconf;
int	procmax;

char dbgflg[256];
static int vflag = 1;

void
optionsinit(char* s)
{
	oargblen = jehanne_strecpy(oargb, oargb+sizeof(oargb), s) - oargb;
	oargc = jehanne_tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

char*
getconf(char *name)
{
	int i;
	char *a;

	for(i = 0; i < oargc; i++){
		a = strstr(oargv[i], name);
		if(a == oargv[i]){
			a += strlen(name);
			if(a[0] == '=')
				return ++a;
		}
	}
	return nil;
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	return 0;
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
				n = jehanne_strtol(argv[0]+1, &p, 0);
				if(p == argv[0]+1 || n < 1 || n > 127)
					n = 1;
				argv[0] = p-1;
				dbgflg[o] = n;
			}
		}else if(jehanne_strcmp(next, "waitgdb") == 0){
			waitdebugger();
		}else if(jehanne_strcmp(next, "idlespin") == 0){
			onIdleSpin();
		}else{
			jehanne_strncpy(envcopy, next, sizeof(envcopy)-1);
			jehanne_gettokens(envcopy, env, 2, "=");
			if(jehanne_strcmp(env[0], "maxcores") == 0){
				maxcores = jehanne_strtol(env[1], 0, 0);
			}
			if(jehanne_strcmp(env[0], "numtcs") == 0){
				numtcs = jehanne_strtol(env[1], 0, 0);
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
			if (jehanne_gettokens(next, env, 2, "=")  == 2){;
				ksetenv(env[0], env[1], 1);
			}else{
				jehanne_print("Ignoring parameter with no value: %s\n", env[0]);
			}
		}
	}
}

void
initialize_processor(void)
{
	int machno;
	Segdesc *gdt;
	uintptr_t *pml4;

	machno = m->machno;
	pml4 = m->pml4;
	gdt = m->gdt;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pml4 = pml4;
	m->gdt = gdt;
	m->perf.period = 1;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;
}

static void
initialize_boot_processor(void)
{
	system.nmach = 1;

	system.machptr[0] = m;	/* m was set by entry.S */

	m->machno = 0;
	m->pml4 = (uint64_t*)CPU0PML4;
	m->gdt = (Segdesc*)CPU0GDT;

	initialize_processor();

	active.machs[0] = 1;
	m->online = 1;
	active.exiting = 0;
}

static void
intialize_system(void)
{
	extern Ureg _boot_registers;
	uintptr_t p;

	p = (uintptr_t)&_boot_registers;
	p += KZERO;
	sys = &system;
	sys->boot_regs = (void*)p;
	sys->architecture = "amd64";
}

static void
configure_kernel(void)
{
	char *p;
	int i, userpcnt;
	unsigned int kpages;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	/* memory */
	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	sys->npages = 0;
	for(i=0; i<nelem(sys->mem); i++)
		sys->npages += sys->mem[i].npage;

	/* processes */
	p = getconf("*procmax");
	if(p != nil)
		sys->nproc = jehanne_strtoull(p, nil, 0);
	if(sys->nproc == 0){
		sys->nproc = 100 + ((sys->npages*PGSZ)/MiB)*5;
		if(cpuserver)
			sys->nproc *= 3;
	}
	if(sys->nproc > 2046){
		/* devproc supports at most (2^11)-2 processes */
		sys->nproc = 2046;
	}
	sys->nimage = 256;

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = sys->npages - (sys->npages*userpcnt)/100;
		sys->nimage = sys->nproc;
	} else {
		if(userpcnt < 10) {
			if(sys->npages*PGSZ < 16*MiB)
				userpcnt = 50;
			else
				userpcnt = 60;
		}
		kpages = sys->npages - (sys->npages*userpcnt)/100;

		/*
		 * Make sure terminals with low memory get at least
		 * 4MB on the first Image chunk allocation.
		 */
		if(sys->npages*PGSZ < 16*MiB)
			imagmem->minarena = 4*MiB;
	}

	/*
	 * can't go past the end of virtual memory.
	 */
	if(kpages > ((uintptr_t)-KZERO)/PGSZ)
		kpages = ((uintptr_t)-KZERO)/PGSZ;

	sys->upages = sys->npages - kpages;
	sys->ialloc = (kpages/2)*PGSZ;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	kpages *= PGSZ;
	kpages -= sys->nproc*sizeof(Proc);
	mainmem->maxsize = kpages;

	/*
	 * the dynamic allocation will balance the load properly,
	 * hopefully. be careful with 32-bit overflow.
	 */
	imagmem->maxsize = kpages - (kpages/10);
	if(p = getconf("*imagemaxmb")){
		imagmem->maxsize = strtol(p, nil, 0)*MiB;
		if(imagmem->maxsize > mainmem->maxsize)
			imagmem->maxsize = mainmem->maxsize;
	}


}

void
main(void)
{
	intialize_system();
	initialize_boot_processor();

	multiboot(0);
	options(oargc, oargv);

	ioinit();
	i8250console();
	fmtinit();
	screen_init();

	jehanne_print("\nJehanne Operating System\n");

	trapinit0();
//	i8259init();

	i8253init();
	cpuidentify();
	meminit();

	configure_kernel();

	xinit();
	archinit();
//	bootscreeninit();
//	if(i8237alloc != nil)
//		i8237alloc();
	trapinit();
	printinit();
	cpuidprint();
	mmuinit();
	if(arch->intrinit)
		arch->intrinit();
	timersinit();
//	keybinit();
//	keybenable();
//	mouseenable();
	mathinit();
	if(arch->clockenable)
		arch->clockenable();

	psinit(sys->nproc);

	links();

	devtabreset();
	umem_init();
	userinit();
	schedinit();
}

void
init0(void)
{
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

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
		jehanne_snprint(buf, sizeof(buf), "%s %s", "AMD64", conffile);
		loadenv(oargc, oargv);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "amd64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	kproc("atimer", awake_timer, 0);
	kproc("aringer", awake_ringer, 0);
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
	jehanne_memmove(p, oargb, i);

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

	/* NOTE: we use the global pointer up so that the kaddr()
	 * (called by segment_fault) can find it
	 */
	up = newproc();
	up->pgrp = newpgrp();
	up->egrp = smalloc(sizeof(Egrp));
	up->egrp->r.ref = 1;
	up->fgrp = dupfgrp(nil);
	up->rgrp = newrgrp();
	up->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&up->text, "*init*");
	kstrdup(&up->user, eve);

	sysprocsetup(up);

	/*
	 * Kernel Stack
	 *
	 * N.B. make sure there's enough space for syscall to check
	 *	for valid args and
	 *	space for gotolabel's return PC
	 * AMD64 stack must be quad-aligned.
	 */
	up->sched.pc = PTR2UINT(init0);
	up->sched.sp = PTR2UINT(up->kstack+KSTACK-sizeof(up->arg)-sizeof(uintptr_t));
	up->sched.sp = STACKALIGN(up->sched.sp);

	/*
	 * User Stack
	 */
	s = nil;
	if(!segment_virtual(&s, SgStack, SgRead|SgWrite, 0, USTKTOP-USTKSIZE, USTKTOP))
		panic("userinit: cannot create stack segment");

	up->seg[SSEG] = s;
	va = USTKTOP-USTKSIZE;
	mmuphys = 0;
	if(!segment_fault(&mmuphys, &va, s, FaultWrite))
		panic("userinit: cannot allocate first stack page");

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
	up->seg[TSEG] = s;

	/*
	 * Data
	 */
	s = nil;
	if(!segment_userinit(&s, 1))
		panic("userinit: cannot create data segment");
	up->seg[DSEG] = s;

	/* reset global pointer up */
	p = up;
	up = nil;
	ready(p);
}

static void
fullstop(void)
{
	splhi();
//	lapicpri(0xff);
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
	adec((int*)&sys->nmach);

	iprint("cpu%d: exiting\n", m->machno);
	/* wait for any other processors to shutdown */
	//spllo();
	prflush();
	for(ms = 10*1000; ms > 0; ms -= 2){
		delay(2);
		if(sys->nmach == 0 && consactive() == 0)
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
