#define	_DBGC_	'F'
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

int
fault(uintptr_t addr, uintptr_t pc, int ftype)
{
	ProcSegment *s;
	char *sps;
	uintptr_t mmuphys, faddr;

	if(up == nil)
		panic("fault: nil up");
	if(up->nlocks){
		panic("fault: %#p %s pc %#p: %s: nlocks %d %#p\n", addr, up->text, pc, up->user, up->nlocks, up->lastlock? lockgetpc(up->lastlock): 0);
		//dumpstack();
	}

	sps = up->psstate;
	up->psstate = "Fault";

	m->pfault++;
	spllo();

	s = proc_segment(up, addr);
	if(s == nil)
		return -1;
	mmuphys = 0;
	faddr = addr; /* remember original address for better errors */
	if(segment_fault(&mmuphys, &addr, s, ftype))
	{
		if(DBGFLG)
			proc_check_pages();
		up->psstate = sps;
		mmuput(addr, mmuphys);
		if(ftype == FaultExecute)
			peekAtExecFaults(addr);
		return 0;
	}

	if(up->procctl == Proc_exitbig)
		pexit("out of memory", 1);

	if(s != nil){
		pprint("%s fault fail %s(%c%c%c) pid %d (%s) addr 0x%p pc 0x%p\n",
			fault_types[ftype],
			segment_types[s->type],
			(s->permissions & SgRead) != 0 ? 'r' : '-',
			(s->permissions & SgWrite) != 0 ? 'w' : '-',
			(s->permissions & SgExecute) != 0 ? 'x' : '-',
			up->pid, up->text, faddr, pc);
	} else {
		pprint("%s fault fail, no segment, pid %d (%s) addr 0x%p pc 0x%p\n",
			fault_types[ftype],
			up->pid, up->text, faddr, pc);
	}
	splhi();
	up->psstate = sps;
	return -1;
}

/*
 * Called only in a system call
 */
int
okaddr(uintptr_t addr, long len, int write)
{
	ProcSegment *s;

	if(len >= 0) {
		for(;;) {
			s = proc_segment(up, addr);
			if(s == 0 || (write && !(s->permissions&SgWrite)))
				break;

			if(addr+len > s->top) {
				len -= s->top - addr;
				addr = s->top;
				continue;
			}
			return 1;
		}
	}
	return 0;
}

void*
validaddr(void* addr, long len, int write)
{
	if(!okaddr(PTR2UINT(addr), len, write)){
		pprint("trap: invalid address %#p/%ld in sys call pc=%#p\n", addr, len, userpc(nil));
		postnote(up, 1, "sys: bad address in syscall", NDebug);
		error(Ebadarg);
	}

	return UINT2PTR(addr);
}

/*
 * &s[0] is known to be a valid address.
 */
void*
vmemchr(void *s, int c, int n)
{
	int np;
	uintptr_t a;
	void *t;

	a = PTR2UINT(s);
	while(ROUNDUP(a, PGSZ) != ROUNDUP(a+n-1, PGSZ)){
		/* spans pages; handle this page */
		np = PGSZ - (a & (PGSZ-1));
		t = jehanne_memchr(UINT2PTR(a), c, np);
		if(t)
			return t;
		a += np;
		n -= np;
		if(!iskaddr(a))
			validaddr(UINT2PTR(a), 1, 0);
	}

	/* fits in one page */
	return jehanne_memchr(UINT2PTR(a), c, n);
}
