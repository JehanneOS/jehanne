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
#include "../port/portfns.h"

#define SUPPORT_MWAIT	(m->cpuinfo[1][2] & (1<<3))
void	onIdleSpin(void);

void	mouseenable(void);
int	mousecmd(int);

void	aamloop(int);
void		acpiinit(int);
Dirtab*	addarchfile(char*, int,
		    long(*)(Chan*,void*,long,int64_t),
		    long(*)(Chan*,void*,long,int64_t));
void	archenable(void);
void	archfmtinstall(void);
void	archidle(void);
void	archinit(void);
int	archmmu(void);
void	archreset(void) __attribute__ ((noreturn));
int64_t	archhz(void);
int	asmfree(uint64_t, uint64_t, int);
uint64_t	asmalloc(uint64_t, uint64_t, int, int);
void	asminit(void);
void	asmmapinit(uint64_t, uint64_t, int);
void 	asmmodinit(uint32_t, uint32_t, char*);
void	cgaconsputs(char*, int);
void	cgainit(void);
void	cgapost(int);
void	(*coherence)(void);
int		corecolor(int);
uint32_t	cpuid(uint32_t, uint32_t, uint32_t[4]);
int	dbgprint(char*, ...);
void	delay(int);
#define	evenaddr(x)				/* x86 doesn't care */
int	e820(void);
int	fpudevprocio(Proc*, void*, int32_t, uintptr_t, int);
void	fpuinit(void);
void	fpunoted(void);
void	fpunotify(Ureg*);
void	fpuprocrestore(Proc*);
void	fpuprocsave(Proc*);
void	fpusysprocsetup(Proc*);
void	fpusysrfork(Ureg*);
void	fpusysrforkchild(Proc*, Proc*);
char*	getconf(char*);
void	_halt(void);
void	halt(void);
void	hpetinit(uint32_t, uint32_t, uintmem, int);
/*int	i8042auxcmd(int);
int	i8042auxcmds(uint8_t*, int);
void	i8042auxenable(void (*)(int, int));*/
void	i8042systemreset(void);
Uart*	i8250console(char*);
void*	i8250alloc(int, int, int);
int64_t	i8254hz(uint32_t[2][4]);
void	idlehands(void);
void	idthandlers(void);
int	inb(int);
void	insb(int, void*, int);
uint16_t	ins(int);
void	inss(int, void*, int);
uint32_t	inl(int);
void	insl(int, void*, int);
int	intrdisable(void*);
void*	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
void	invlpg(uintptr_t va);
void	iofree(int);
void	ioinit(void);
int	iounused(int, int);
int	ioalloc(int, int, int, char*);
int	ioreserve(int, int, int, char*);
int	iprint(char*, ...);
int	isaconfig(char*, int, ISAConf*);
int	isdmaok(void*, usize, int);
void	keybenable(void);		// 386/i8042.c
void	keybinit(void);			// 386/i8042.c
void	kexit(Ureg*);
#define	kmapinval()
void	lfence(void);
void	links(void);
int	machdom(Mach*);
void	machinit(void);
void	mach0init(void);
void	mapraminit(uint64_t, uint64_t);
void	memdebug(void);
void	meminit(void);
int		memcolor(uintmem addr, uintmem *sizep);
void	memmaprange(uintptr_t, uintmem, uintmem, PTE (*alloc)(usize), PTE);
void	memreserve(uintmem, uintmem);
void	mfence(void);
void	mmudump(Proc*);
void	mmuflushtlb(uint64_t);
void	mmuinit(void);
#define	mmucachectl(pg, why)	USED(pg, why)	/* x86 doesn't need it */
uint64_t	mmuphysaddr(uintptr_t);
int	mmuwalk(uintptr_t, int, PTE**, uint64_t (*)(usize));
int	multiboot(uint32_t, uint32_t, int);
uint32_t	mwait32(void*, uint32_t);
uint64_t	mwait64(void*, uint64_t);
void	ndnr(void);
uint8_t	nvramread(int);
void	nvramwrite(int, uint8_t);
void	optionsinit(char*);
void	outb(int, int);
void	outsb(int, void*, int);
void	outs(int, uint16_t);
void	outss(int, void*, int);
void	outl(int, uint32_t);
void	outsl(int, void*, int);
void	pause(void);
int	pciscan(int, Pcidev**);
uint32_t	pcibarsize(Pcidev*, int);
int	pcicap(Pcidev*, int);
int	pcicfgr8(Pcidev*, int);
int	pcicfgr16(Pcidev*, int);
uint32_t	pcicfgr32(Pcidev*, int);
void	pcicfgw8(Pcidev*, int, int);
void	pcicfgw16(Pcidev*, int, int);
void	pcicfgw32(Pcidev*, int, int);
void	pciclrbme(Pcidev*);
void	pciclrioe(Pcidev*);
void	pciclrmwi(Pcidev*);
int	pcigetpms(Pcidev*);
void	pcihinv(Pcidev*);
uint8_t	pciipin(Pcidev*, uint8_t);
Pcidev* pcimatch(Pcidev*, int, int);
Pcidev* pcimatchtbdf(int);
void	pcireset(void);
void	pcisetbme(Pcidev*);
void	pcisetioe(Pcidev*);
void	pcisetmwi(Pcidev*);
int	pcisetpms(Pcidev*, int);
uintmem	pcixcfgspace(int);
void*	pcixcfgaddr(Pcidev*, int);
void	printcpufreq(void);
int	screenprint(char*, ...);			/* debugging */
void	sfence(void);
void	spldone(void);
void	(*specialmem)(uintmem, uintmem, int);
uint64_t	splhi(void);
uint64_t	spllo(void);
void	splx(uint64_t);
void	splxpc(uint64_t);
void	syncclock(void);
void*	sysexecregs(uintptr_t, uint32_t);
uintptr_t	sysexecstack(uintptr_t, int);
void	sysprocsetup(Proc*);
void	tssrsp0(uint64_t);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit(void);
int	userureg(Ureg*);
void*	vmap(uintmem, usize);
void	vsvminit(int);
void	vunmap(void*, usize);

extern Mreg cr0get(void);
extern void cr0put(Mreg);
extern Mreg cr2get(void);
extern Mreg cr3get(void);
extern void cr3put(Mreg);
extern Mreg cr4get(void);
extern void cr4put(Mreg);
extern void gdtget(void*);
extern void gdtput(int, uint64_t, uint16_t);
extern void idtput(int, uint64_t);
extern uint64_t rdmsr(uint32_t);
extern uint64_t rdtsc(void);
extern void trput(uint64_t);
extern void wrmsr(uint32_t, uint64_t);
int	xaddb(void*);

extern int islo(void);
extern void spldone(void);
extern Mreg splhi(void);
extern Mreg spllo(void);
extern void splx(Mreg);

int	cas8(void*, uint8_t, uint8_t);
int	cas16(void*, uint16_t, uint16_t);
int	cas32(void*, uint32_t, uint32_t);
int	cas64(void*, uint64_t, uint64_t);
int	tas32(void*);

#define CASU(p, e, n)	cas64((p), (uint64_t)(e), (uint64_t)(n))
#define CASV(p, e, n)	cas64((p), (uint64_t)(e), (uint64_t)(n))
#define CASW(p, e, n)	cas32((p), (e), (n))
#define TAS(addr)	tas32((addr))

void	touser(uintptr_t);
void	syscallentry(void);
void	syscallreturn(void);
void	sysrforkret(void);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define	dcflush(a, b)

#define PTR2UINT(p)	((uintptr_t)(p))
#define UINT2PTR(i)	((void*)(i))

void*	KADDR(uintmem);
uintptr_t	PADDR(void*);

#define BIOSSEG(a)	KADDR(((uint32_t)(a))<<4)

/*
 * archk10.c
 */
extern void millidelay(int);

/*
 * i8259.c
 */
extern int i8259init(int);
extern int i8259irqdisable(int);
extern int i8259irqenable(int);
extern int i8259isr(int);

/*
 * sipi.c
 */
extern void sipi(void);

void*	basealloc(usize, uint32_t, usize*);
void	basefree(void*, usize);
void	physallocinit(void);
void	uartpush(void);

/* horror */
static inline void __clobber_callee_regs(void)
{
	asm volatile ("" : : : "rbx", "r12", "r13", "r14", "r15");
}

int slim_setlabel(Label*) __attribute__((returns_twice));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

#define setlabel(label) ({int err;                                                 \
                    __clobber_callee_regs();                               \
                    err = slim_setlabel(label);                                     \
                    err;})

#pragma GCC diagnostic pop

