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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<ptrace.h>
#include	"ureg.h"

#include	"io.h"
#include	"apic.h"

#include	"amd64.h"

static int trapinited;

extern int notify(Ureg*);

static void debugbpt(Ureg*, void*);
static void faultamd64(Ureg*, void*);
static void doublefault(Ureg*, void*);
static void unexpected(Ureg*, void*);
static void dumpstackwithureg(Ureg*);

static Lock vctllock;
static Vctl *vctl[256];

enum
{
	Ntimevec = 20		/* number of time buckets for each intr */
};
uint32_t intrtimes[256][Ntimevec];

void*
intrenable(int irq, void (*f)(Ureg*, void*), void* a, int tbdf, char *name)
{
	int vno;
	Vctl *v;

	if(f == nil){
		print("intrenable: nil handler for %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		return nil;
	}

	if(tbdf != BUSUNKNOWN && (irq == 0xff || irq == 0)){
		print("intrenable: got unassigned irq %d, tbdf 0x%uX for %s\n",
			irq, tbdf, name);
		irq = -1;
	}

	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("intrenable: out of memory");
	v->isintr = 1;
	v->irq = irq;
	v->tbdf = tbdf;
	v->f = f;
	v->a = a;
	jehanne_strncpy(v->name, name, KNAMELEN-1);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	vno = arch->intrenable(v);
	if(vno == -1){
		iunlock(&vctllock);
		print("intrenable: couldn't enable irq %d, tbdf 0x%uX for %s\n",
			irq, tbdf, v->name);
		xfree(v);
		return nil;
	}
	if(vctl[vno]){
		if(vctl[vno]->isr != v->isr || vctl[vno]->eoi != v->eoi)
			panic("intrenable: handler: %s %s %#p %#p %#p %#p",
				vctl[vno]->name, v->name,
				vctl[vno]->isr, v->isr, vctl[vno]->eoi, v->eoi);
		v->next = vctl[vno];
	}
	vctl[vno] = v;
	iunlock(&vctllock);

	/*
	 * Return the assigned vector so intrdisable can find
	 * the handler; the IRQ is useless in the wondrefule world
	 * of the IOAPIC.
	 */
	return v;
}

int
intrdisable(int irq, void (*f)(Ureg *, void *), void *a, int tbdf, char *name)
{
	Vctl **pv, *v;
	int vno;

	if(arch->intrvecno == nil || (tbdf != BUSUNKNOWN && (irq == 0xff || irq == 0))){
		/*
		 * on APIC machine, irq is pretty meaningless
		 * and disabling a the vector is not implemented.
		 * however, we still want to remove the matching
		 * Vctl entry to prevent calling Vctl.f() with a
		 * stale Vctl.a pointer.
		 */
		irq = -1;
		vno = VectorPIC;
	} else {
		vno = arch->intrvecno(irq);
	}
	ilock(&vctllock);
	do {
		for(pv = &vctl[vno]; (v = *pv) != nil; pv = &v->next){
			if(v->isintr && (v->irq == irq || irq == -1)
			&& v->tbdf == tbdf && v->f == f && v->a == a
			&& strcmp(v->name, name) == 0)
				break;
		}
		if(v != nil){
			*pv = v->next;
			xfree(v);

			if(irq != -1 && vctl[vno] == nil && arch->intrdisable != nil)
				arch->intrdisable(irq);
			break;
		}
	} while(irq == -1 && ++vno <= MaxVectorAPIC);
	iunlock(&vctllock);
	return 0;
}

static long
irqallocread(Chan* _1, void *vbuf, long n, int64_t offset)
{
	char *buf, *p, str[2*(11+1)+KNAMELEN+1+1];
	int ns, vno;
	long oldn;
	Vctl *v;

	if(n < 0 || offset < 0)
		error(Ebadarg);

	oldn = n;
	buf = vbuf;
	for(vno=0; vno<nelem(vctl); vno++){
		for(v=vctl[vno]; v; v=v->next){
			ns = jehanne_snprint(str, sizeof str, "%11d %11d %.*s\n", vno, v->irq, KNAMELEN, v->name);
			if(ns <= offset)	/* if do not want this, skip entry */
				offset -= ns;
			else{
				/* skip offset bytes */
				ns -= offset;
				p = str+offset;
				offset = 0;

				/* write at most max(n,ns) bytes */
				if(ns > n)
					ns = n;
				jehanne_memmove(buf, p, ns);
				n -= ns;
				buf += ns;

				if(n == 0)
					return oldn;
			}
		}
	}
	return oldn - n;
}

void
trapenable(int vno, void (*f)(Ureg*, void*), void* a, char *name)
{
	Vctl *v;

	if(vno < 0 || vno >= VectorPIC)
		panic("trapenable: vno %d", vno);
	if((v = xalloc(sizeof(Vctl))) == nil)
		panic("trapenable: out of memory");
	v->tbdf = BUSUNKNOWN;
	v->f = f;
	v->a = a;
	jehanne_strncpy(v->name, name, KNAMELEN);
	v->name[KNAMELEN-1] = 0;

	ilock(&vctllock);
	if(vctl[vno])
		v->next = vctl[vno]->next;
	vctl[vno] = v;
	iunlock(&vctllock);
}

static void
nmienable(void)
{
	int x;

	/*
	 * Hack: should be locked with NVRAM access.
	 */
	outb(0x70, 0x80);		/* NMI latch clear */
	outb(0x70, 0);

	x = inb(0x61) & 0x07;		/* Enable NMI */
	outb(0x61, 0x0C|x);
	outb(0x61, x);
}

void
trapinit0(void)
{
	uint32_t d1, v;
	uintptr_t vaddr;
	Segdesc *idt;

	idt = (Segdesc*)IDTADDR;
	vaddr = (uintptr_t)idthandlers;
	for(v = 0; v < 256; v++){
		d1 = (vaddr & 0xFFFF0000)|SEGP;
		switch(v){

		case VectorBPT:
			d1 |= SEGPL(3)|SEGIG;
			break;

		case VectorSYSCALL:
			d1 |= SEGPL(3)|SEGIG;
			break;

		default:
			d1 |= SEGPL(0)|SEGIG;
			break;
		}

		idt->d0 = (vaddr & 0xFFFF)|(KESEL<<16);
		idt->d1 = d1;
		idt++;

		idt->d0 = (vaddr >> 32);
		idt->d1 = 0;
		idt++;

		vaddr += 6;
	}
}

void
trapinit(void)
{
	/*
	 * Need to set BPT interrupt gate - here or in vsvminit?
	 */
	/*
	 * Special traps.
	 * Syscall() is called directly without going through trap().
	 */
//	trapenable(VectorDE, debugexc, 0, "debugexc");
	trapenable(VectorBPT, debugbpt, 0, "debugpt");
	trapenable(VectorPF, faultamd64, 0, "faultamd64");
	trapenable(Vector2F, doublefault, 0, "doublefault");
	trapenable(Vector15, unexpected, 0, "unexpected");
	nmienable();

	addarchfile("irqalloc", 0444, irqallocread, nil);
	trapinited = 1;
}

static char* excname[32] = {
	"divide error",
	"debug exception",
	"nonmaskable interrupt",
	"breakpoint",
	"overflow",
	"bounds check",
	"invalid opcode",
	"coprocessor not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid TSS",
	"segment not present",
	"stack exception",
	"general protection violation",
	"page fault",
	"15 (reserved)",
	"coprocessor error",
	"alignment check",
	"machine check",
	"simd error",
	"20 (reserved)",
	"21 (reserved)",
	"22 (reserved)",
	"23 (reserved)",
	"24 (reserved)",
	"25 (reserved)",
	"26 (reserved)",
	"27 (reserved)",
	"28 (reserved)",
	"29 (reserved)",
	"30 (reserved)",
	"31 (reserved)",
};

/*
 *  keep histogram of interrupt service times
 */
void
intrtime(Mach* _1, int vno)
{
	uint64_t diff, x;

	x = perfticks();
	diff = x - m->perf.intrts;
	m->perf.intrts = x;

	m->perf.inintr += diff;
	if(up == nil && m->perf.inidle > diff)
		m->perf.inidle -= diff;

	diff /= m->cpumhz*100;	// quantum = 100µsec
	if(diff >= Ntimevec)
		diff = Ntimevec-1;
	intrtimes[vno][diff]++;
}

/* go to user space */
void
kexit(Ureg* _1)
{
	/* used to serve precise time accounting, at kernel exit */
}

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.  The only exception is
 *  for a system call.
 *  Trap is called with interrupts disabled via interrupt-gates.
 */
void
trap(Ureg* ureg)
{
	int clockintr, vno, user, i;
	char buf[ERRMAX];
	Vctl *ctl, *v;

	if(!trapinited){
		/* faultamd64 can give a better error message */
		if(ureg->type == VectorPF)
			faultamd64(ureg, nil);
		panic("trap %llud: not ready", ureg->type);
	}

	m->perf.intrts = perfticks();
	user = userureg(ureg);
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	clockintr = 0;

	vno = ureg->type;

	if(ctl = vctl[vno]){
		if(ctl->isintr){
			m->intr++;
			if(vno >= VectorPIC)
				m->lastintr = ctl->irq;
		}
		if(ctl->isr)
			ctl->isr(vno);
		for(v = ctl; v != nil; v = v->next){
			if(v->f)
				v->f(ureg, v->a);
		}
		if(ctl->eoi)
			ctl->eoi(vno);

		if(ctl->isintr){
			intrtime(m, vno);

			if(ctl->irq == IrqCLOCK || ctl->irq == IrqTIMER)
				clockintr = 1;

			if(up && !clockintr)
				preempted();
		}
	}
	else if(vno < nelem(excname) && user){
		spllo();
		if(vno == 0xd)	/* General Protection */
			dumpregs(ureg);
		jehanne_snprint(buf, sizeof(buf), "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
	} else if(vno >= VectorPIC){
		/*
		 * An unknown interrupt.
		 * Check for a default IRQ7. This can happen when
		 * the IRQ input goes away before the acknowledge.
		 * In this case, a 'default IRQ7' is generated, but
		 * the corresponding bit in the ISR isn't set.
		 * In fact, just ignore all such interrupts.
		 */

		/* call all interrupt routines, just in case */
		for(i = VectorPIC; i <= MaxIrqLAPIC; i++){
			ctl = vctl[i];
			if(ctl == nil)
				continue;
			if(!ctl->isintr)
				continue;
			for(v = ctl; v != nil; v = v->next){
				if(v->f)
					v->f(ureg, v->a);
			}
			/* should we do this? */
			if(ctl->eoi)
				ctl->eoi(i);
		}

		/* clear the interrupt */
		i8259isr(vno);

//		m->spuriousintr++;
		if(user)
			kexit(ureg);
		return;
	}
	else{
		if(vno == VectorNMI){
			/*
			 * Don't re-enable, it confuses the crash dumps.
			nmienable();
			 */
			iprint("cpu%d: nmi PC %#p, status %ux\n",
				m->machno, ureg->ip, inb(0x61));
			while(m->machno != 0)
				;
		}

		if(!user){
			void (*pc)(void);

			extern void _rdmsrinst(void);
			extern void _wrmsrinst(void);

			pc = (void*)ureg->ip;
			if(pc == _rdmsrinst || pc == _wrmsrinst){
				if(vno == VectorGPF){
					ureg->bp = -1;
					ureg->ip += 2;
					return;
				}
			}
		}

		dumpregs(ureg);
		if(vno < nelem(excname))
			panic("%s", excname[vno]);
		panic("unknown trap/intr: %d", vno);
	}
	splhi();

	/* delaysched set because we held a lock or because our quantum ended */
	if(up && up->delaysched && clockintr){
		sched();
		splhi();
	}

	if(user){
		if(up->procctl || up->nnote)
			notify(ureg);
		kexit(ureg);
	}
}

/*
 * Dump general registers.
 */
static void
dumpgpr(Ureg* ureg)
{
	if(up != nil)
		iprint("cpu%d: registers for %s %d [%#p]\n",
			m->machno, up->text, up->pid, getcallerpc());
	else
		iprint("cpu%d: registers for kernel\n", m->machno);
if(1){
	iprint("ax\t%#16.16llux ", ureg->ax);
	iprint("bx\t%#16.16llux\n", ureg->bx);
	iprint("cx\t%#16.16llux ", ureg->cx);
	iprint("dx\t%#16.16llux\n", ureg->dx);
	iprint("di\t%#16.16llux ", ureg->di);
	iprint("si\t%#16.16llux\n", ureg->si);
	iprint("bp\t%#16.16llux ", ureg->bp);
	iprint("r8\t%#16.16llux\n", ureg->r8);
	iprint("r9\t%#16.16llux ", ureg->r9);
	iprint("r10\t%#16.16llux\n", ureg->r10);
	iprint("r11\t%#16.16llux ", ureg->r11);
	iprint("r12\t%#16.16llux\n", ureg->r12);
	iprint("r13\t%#16.16llux ", ureg->r13);
	iprint("r14\t%#16.16llux\n", ureg->r14);
	iprint("r15\t%#16.16llux\n", ureg->r15);
}
	iprint("ds  %#4.4ux   es  %#4.4ux   fs  %#4.4ux   gs  %#4.4ux\n",
		0, 0, 0, 0);
//		ureg->ds, ureg->es, ureg->fs, ureg->gs);
	iprint("type\t%#llux ", ureg->type);
	iprint("error\t%#llux\n", ureg->error);
	iprint("pc\t%#llux ", ureg->ip);
	iprint("cs\t%#llux\n", ureg->cs);
	iprint("flags\t%#llux\n", ureg->flags);
	iprint("sp\t%#llux ", ureg->sp);
	iprint("ss\t%#llux\n", ureg->ss);
	iprint("type\t%#llux\n", ureg->type);

	iprint("m\t%#16.16p up\t%#16.16p\n", m, up);
}

void
dumpregs(Ureg* ureg)
{
	iprint("dumpregs: %#p ", getcallerpc());
	dumpgpr(ureg);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	iprint("cr0\t%#16.16llux\n", cr0get());
	iprint("cr2\t%#16.16llux\n", cr2get());
	iprint("cr3\t%#16.16llux\n", cr3get());

//	archdumpregs();
}

/*
 * Fill in enough of Ureg to get a stack trace, and call a function.
 * Used by debugging interface rdb.
 */
void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;
	jehanne_memset(&ureg, 0, sizeof(ureg));
	ureg.ip = getcallerpc();
	ureg.sp = PTR2UINT(&fn);
	fn(&ureg);
}

static void
dumpstackwithureg(Ureg* ureg)
{
	uintptr_t l, v, i, estack;
	extern char etext[];
	char *s;
	int x;

	if((s = getconf("*nodumpstack")) != nil && jehanne_atoi(s) != 0){
		iprint("dumpstack disabled\n");
		return;
	}

	x = 0;
	x += iprint("ktrace /kernel/path %#p %#p\n", ureg->ip, ureg->sp);
	i = 0;
	if(up != nil
	&& (uintptr_t)&l >= (uintptr_t)up->kstack
	&& (uintptr_t)&l <= (uintptr_t)up->kstack+KSTACK)
		estack = (uintptr_t)up->kstack+KSTACK;
	else if((uintptr_t)&l >= m->stack && (uintptr_t)&l <= m->stack+MACHSTKSZ)
		estack = m->stack+MACHSTKSZ;
	else{
		if(up != nil)
			iprint("&up->kstack %#p &l %#p\n", up->kstack, &l);
		else
			iprint("&m %#p &l %#p\n", m, &l);
		return;
	}
	x += iprint("estackx %#p\n", estack);

	for(l = (uintptr_t)&l; l < estack; l += sizeof(uintptr_t)){
		v = *(uintptr_t*)l;
		if((KTZERO < v && v < (uintptr_t)&etext)
		|| ((uintptr_t)&l < v && v < estack) || estack-l < 256){
			x += iprint("%#16.16p=%#16.16p ", l, v);
			i++;
		}
		if(i == 2){
			i = 0;
			x += iprint("\n");
		}
	}
	if(i)
		iprint("\n");
}

void
dumpstack(void)
{
	callwithureg(dumpstackwithureg);
}

static void
debugbpt(Ureg* ureg, void* _1)
{
	char buf[ERRMAX];

	if(up == 0)
		panic("kernel bpt");
	/* restore pc to instruction that caused the trap */
	ureg->ip--;
	jehanne_sprint(buf, "sys: breakpoint");
	postnote(up, 1, buf, NDebug);
}

static void
doublefault(Ureg* _1, void* _2)
{
	panic("double fault");
}

static void
unexpected(Ureg* ureg, void* _1)
{
	iprint("unexpected trap %llud; ignoring\n", ureg->type);
}

static void
faultamd64(Ureg* ureg, void* _1)
{
	uint64_t addr, arg;
	int ftype, user, inkernel;
	Syscalls cursyscall;
	char buf[ERRMAX];
	void (*pt)(Proc*, int, int64_t, int64_t);

	addr = cr2get();
	if(ureg->error&2)
		ftype = FaultWrite;
	else if(ureg->error&16)
		ftype = FaultExecute;
	else
		ftype = FaultRead;
	user = userureg(ureg);
	if(!user){
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=%#p addr=%#p", ureg->ip, addr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#p addr=%#p", ureg->ip, addr);
	}
	if(up == nil)
		panic("user fault: up=0 pc=%#p addr=%#p", ureg->ip, addr);

	if(up->trace && (pt = proctrace) != nil){
		if(ftype == FaultWrite)
			arg = STrapWPF | (addr&STrapMask);
		else
			arg = STrapRPF | (addr&STrapMask);
		pt(up, STrap, 0, arg);
	}

	inkernel = up->inkernel;
	cursyscall = up->cursyscall;
	up->inkernel = 1;
	up->cursyscall = 0;
if(iskaddr(addr)){
	jehanne_print("kaddr %#llux pc %#p\n", addr, ureg->ip);
//	prflush();
	dumpregs(ureg);
}
	if(fault(addr, ureg->ip, ftype) < 0){
		if(!user){
			dumpregs(ureg);
			panic("fault: %#llux pc %#p\n", addr, ureg->ip);
		}
		jehanne_sprint(buf, "sys: trap: fault %s addr=%#llux",
			fault_types[ftype], addr);
		proc_check_pages();
		postnote(up, 1, buf, NDebug);
		if(inkernel){
			up->cursyscall = cursyscall;
			error(buf);
		}
	}
	up->cursyscall = cursyscall;
	up->inkernel = inkernel;
}

/*
 *  return the userpc the last exception happened at
 */
uintptr_t
userpc(Ureg* ureg)
{
	if(ureg == nil)
		ureg = up->dbgreg;
	return ureg->ip;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	uint64_t cs, flags, ss;
//	uint16_t ds, es, fs, gs;

	ss = ureg->ss;
	flags = ureg->flags;
	cs = ureg->cs;
//	gs = ureg->cs;
//	fs = ureg->cs;
//	es = ureg->cs;
//	ds = ureg->cs;
	jehanne_memmove(pureg, uva, n);
//	ureg->ds = ds;
//	ureg->es = es;
//	ureg->fs = fs;
//	ureg->gs = gs;
	ureg->cs = cs;
	ureg->flags = (ureg->flags & 0x00ff) | (flags & 0xff00);
	ureg->ss = ss;
}

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->ip = p->sched.pc;
	ureg->sp = p->sched.sp+BY2SE;
}

uintptr_t
dbgpc(Proc *p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;

	return ureg->ip;
}
