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

int	bios32call(BIOS32ci*, uint16_t[3]);
int	bios32ci(BIOS32si*, BIOS32ci*);
void	bios32close(BIOS32si*);
BIOS32si* bios32open(char*);
void*	sigsearch(char*);
int	checksum(void *v, int n);

void	mouseenable(void);
int	mousecmd(int);

void	aamloop(int);
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
void	screen_init(void);
uintptr_t cankaddr(uintptr_t pa);
void	(*coherence)(void);
void	cpuid(int, uint32_t regs[]);
int	cpuidentify(void);
void	cpuidprint(void);
int		corecolor(int);
void	(*cycles)(uint64_t*);
int	dbgprint(char*, ...);
void	delay(int);
int	ecinit(int cmdport, int dataport);
int	ecread(uint8_t addr);
int	ecwrite(uint8_t addr, uint8_t val);
#define	evenaddr(x)				/* x86 doesn't care */
int	e820(void);
void	fpclear(void);
void	fpinit(void);
void	fpoff(void);
void	(*fprestore)(FPsave*);
void	(*fpsave)(FPsave*);
void	fpsserestore(FPsave*);
void	fpssesave(FPsave*);
void	fpprocfork(Proc *p);
void	fpprocsetup(Proc* p);
char*	getconf(char*);
void	guesscpuhz(int);
void	_halt(void);
void	halt(void);
void	hpetinit(uint32_t, uint32_t, uintmem, int);
int	i8042auxcmd(int);
void	i8042auxenable(void (*)(int, int));
void	i8042reset(void);
void	i8250console(void);
void*	i8250alloc(int, int, int);
int64_t	i8254hz(uint32_t[2][4]);

void	i8253enable(void);
void	i8253init(void);
void	i8253reset(void);
uint64_t	i8253read(uint64_t*);
void	i8253timerset(uint64_t);

void	idle(void);
void	idlehands(void);
void	idthandlers(void);
int	inb(int);
void	insb(int, void*, int);
uint16_t	ins(int);
void	inss(int, void*, int);
uint32_t	inl(int);
void	insl(int, void*, int);
int	intrdisable(int irq, void (*f)(Ureg *, void *), void *a, int tbdf, char *name);
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
void	keybenable(void);		// i8042.c
void	keybinit(void);			// i8042.c
void	kexit(Ureg*);
KMap*	kmap(uintptr_t pa);
void	kunmap(KMap*);
#define	kmapinval()
void	lfence(void);
void	links(void);
#define	lockgetpc(l) (l->pc)
int	machdom(Mach*);
void	machinit(void);
void	mach0init(void);
void	mapraminit(uint64_t, uint64_t);
void	mathinit(void);
void	memdebug(void);
void	meminit(void);
int		memcolor(uintmem addr, uintmem *sizep);
void	memmaprange(uintptr_t, uintmem, uintmem, PTE (*alloc)(usize), PTE);
void	memreserve(uintmem, uintmem);
void	mfence(void);
void	mmudump(Proc*);
#define mmuflushtlb() cr3put(cr3get())
void	mmuinit(void);
#define	mmucachectl(pg, why)	USED(pg, why)	/* x86 doesn't need it */
uint64_t	mmuphysaddr(uintptr_t);
uintptr_t*	mmuwalk(uintptr_t* table, uintptr_t va, int level, int create);
char*	mtrr(unsigned long, unsigned long, char *);
void	mtrrclock(void);
int	mtrrprint(char *, long);
void	mtrrsync(void);
int	multiboot(int);
void	mwait(void*);
uint32_t	mwait32(void*, uint32_t);
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
void	patwc(void *a, int n);
void	pause(void);
int	pciscan(int, Pcidev**);
uint32_t	pcibarsize(Pcidev*, int);
int	pcicap(Pcidev*, int);
int	pcicfgr8(Pcidev*, int);
int	pcicfgr16(Pcidev*, int);
int	pcicfgr32(Pcidev*, int);
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
void	pmap(uintptr_t *pml4, uintptr_t pa, uintptr_t va, long size);
void	printcpufreq(void);
void*	rampage(void);
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
uint64_t	tscticks(uint64_t *hz);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit(void);
void	trapinit0(void);
int	userureg(Ureg*);
uintptr_t	upaalloc(int size, int align);
void		upafree(uintptr_t pa, int size);
void		upareserve(uintptr_t pa, int size);
void*	vmap(uintptr_t, usize);
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
extern void lgdt(void*);
extern void idtput(int, uint64_t);
extern void lidt(void*);
extern int rdmsr(uint32_t reg, long* value);
extern uint64_t rdtsc(void);
extern void trput(uint64_t);
extern void wbinvd(void);
extern int wrmsr(uint32_t, uint64_t);
int	xaddb(void*);

#define	userureg(ur)	(((ur)->cs & 3) == 3)

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

uintptr_t	mmu_physical_address(void*);
void*		mmu_kernel_address(uintptr_t);
#define	KADDR(a)	mmu_kernel_address(a)
#define PADDR(a)	mmu_physical_address((void*)(a))

#define BIOSSEG(a)	KADDR(((uint32_t)(a))<<4)

/*
 * archk10.c
 */
extern void millidelay(int);

/*
 * i8259.c
 */
extern void i8259init(void);
extern int i8259disable(int);
extern int i8259enable(Vctl* v);
extern int i8259isr(int);
extern void i8259on(void);
extern void i8259off(void);
extern int i8259vecno(int irq);

/*
 * sipi.c
 */
extern void sipi(void);

void*	basealloc(usize, uint32_t, usize*);
void	basefree(void*, usize);
void	physallocinit(void);
void	uartpush(void);

void	rdrandbuf(void*, uint32_t);


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

