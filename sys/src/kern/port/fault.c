#define	_DBGC_	'F'
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

//char *faulttypes[] = {
	//[FT_WRITE] "write",
	//[FT_READ] "read",
	//[FT_EXEC] "exec"
//};


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
		print("%s fault fail %s(%c%c%c) pid %d (%s) addr 0x%p pc 0x%p\n",
			fault_types[ftype],
			segment_types[s->type],
			(s->permissions & SgRead) != 0 ? 'r' : '-',
			(s->permissions & SgWrite) != 0 ? 'w' : '-',
			(s->permissions & SgExecute) != 0 ? 'x' : '-',
			up->pid, up->text, faddr, pc);
	} else {
		print("%s fault fail, no segment, pid %d (%s) addr 0x%p pc 0x%p\n",
			fault_types[ftype],
			up->pid, up->text, faddr, pc);
	}
	splhi();
	up->psstate = sps;
	return -1;
}

//int
//fixfault(Segment *s, uintptr_t addr, int ftype, int dommuput)
//{
	//int type;
	//Pte **p, *etp;
	//uintptr_t soff;
	//uintmem mmuphys;
	//Page **pg, *old, *new;
	//Page *(*fn)(Segment*, uintptr_t);
	//uintptr_t pgsize;
	//Pages *pages;

	//pages = s->pages;	/* TO DO: segwalk */
	//pgsize = 1<<pages->lg2pgsize;
	//addr &= ~(pgsize-1);
	//soff = addr-s->pages->base;

	//p = &pages->map[soff/pages->ptemapmem];
	//if(*p == nil)
		//*p = ptealloc();

	//etp = *p;
	//pg = &etp->pages[(soff&(pages->ptemapmem-1))>>pages->lg2pgsize];

	//if(pg < etp->first)
		//etp->first = pg;
	//if(pg > etp->last)
		//etp->last = pg;

	//type = s->type&SG_TYPE;
	//if(*pg == nil){
		//switch(type){
		//case SG_BSS:			/* Zero fill on demand */
		//case SG_SHARED:
		//case SG_STACK:
			//new = newpage(1, s->pages->lg2pgsize, &s->lk);
			//if(new == nil)
				//return -1;
			//*pg = new;
			//break;

		//case SG_LOAD:
		//case SG_TEXT:	/* demand load */
		//case SG_DATA:
			//if(!loadimagepage(s->image, s, pg, addr))
				//return -1;
			//break;

		//case SG_PHYSICAL:
			//fn = s->pseg->pgalloc;
			//if(fn != nil)
				//*pg = (*fn)(s, addr);
			//else {
				//new = smalloc(sizeof(Page));
				//new->pa = s->pseg->pa+(addr-s->pages->base);
				//new->r.ref = 1;
				//new->lg2size = s->pseg->lg2pgsize;
				//if(new->lg2size == 0)
					//new->lg2size = PGSHFT;	/* TO DO */
				//*pg = new;
			//}
			//break;
		//default:
			//panic("fault on demand");
			//break;
		//}
	//}
	//mmuphys = 0;
	//switch(type) {
	//default:
		//panic("fault");
		//break;

	//case SG_TEXT:
		//DBG("text pg %#p: %#p -> %#P %d\n", pg, addr, (*pg)->pa, (*pg)->r.ref);
		//mmuphys = PPN((*pg)->pa) | PTERONLY|PTEVALID;
		//break;

	//case SG_BSS:
	//case SG_SHARED:
	//case SG_STACK:
	//case SG_DATA:			/* copy on write */
		//DBG("data pg %#p: %#p -> %#P %d\n", pg, addr, (*pg)->pa, (*pg)->r.ref);
		///*
		 //*  It's only possible to copy on write if
		 //*  we're the only user of the segment.
		 //*/
		//if(ftype != FT_WRITE && sys->copymode == 0 && s->r.ref == 1) {
			//mmuphys = PPN((*pg)->pa)|PTERONLY|PTEVALID;
			//break;
		//}

		//old = *pg;
		//if(old->r.ref > 1){
			///* shared (including image pages): make private writable copy */
			//new = newpage(0, s->pages->lg2pgsize, &s->lk);
			//if(new != nil)
				//copypage(old, new);
			//*pg = new;
			//putpage(old);
			//if(new == nil)
				//return -1;
			//DBG("data' pg %#p: %#p -> %#P %d\n", *pg, addr, old->pa, old->r.ref);
		//}else if(old->r.ref <= 0)
			//panic("fault: page %#p %#P ref %d <= 0", old, old->pa, old->r.ref);
		//mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEVALID;
		//break;

	//case SG_PHYSICAL:
		//mmuphys = PPN((*pg)->pa) | PTEVALID;
		//if((s->pseg->attr & SG_WRITE))
			//mmuphys |= PTEWRITE;
		//if((s->pseg->attr & SG_CACHED) == 0)
			//mmuphys |= PTEUNCACHED;
		//break;
	//}
	//runlock(&s->lk);

	//if(dommuput)
		//mmuput(addr, mmuphys, *pg);
	//if(ftype == FT_EXEC)
		//peekAtExecFaults(addr);

	//return 0;
//}

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
		pprint("trap: invalid address %#p/%lud in sys call pc=%#P\n", addr, len, userpc(nil));
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
		t = memchr(UINT2PTR(a), c, np);
		if(t)
			return t;
		a += np;
		n -= np;
		if(!iskaddr(a))
			validaddr(UINT2PTR(a), 1, 0);
	}

	/* fits in one page */
	return memchr(UINT2PTR(a), c, n);
}
