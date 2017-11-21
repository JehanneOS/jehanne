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

#include "../port/error.h"

#include <ptrace.h>

#include "amd64.h"
#include "ureg.h"

extern int printallsyscalls;

typedef struct {
	uintptr_t	ip;
	Ureg*	arg0;
	char*	arg1;
	char	msg[ERRMAX];
	Ureg*	old;
	Ureg	ureg;
} NFrame;

/*
 *   Return user to state before notify()
 */
static void
noted(Ureg* cur, uintptr_t arg0)
{
	NFrame *nf;
	Ureg *nur;

	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("suicide: call to noted when not notified\n");
		pexit("Suicide", 0);
	}
	awake_gc_note(up);
	up->notified = 0;

	nf = up->ureg;

	up->fpstate &= ~FPillegal;

	/* sanity clause */
	if(!okaddr(PTR2UINT(nf), sizeof(NFrame), 0)){
		qunlock(&up->debug);
		pprint("suicide: bad ureg %#p in noted\n", nf);
		pexit("Suicide", 0);
	}

	/*
	 * Check the segment selectors are all valid.
	 */
	nur = &nf->ureg;
	if(nur->cs != SSEL(SiUCS, SsRPL3) || nur->ss != SSEL(SiUDS, SsRPL3)
//	|| nur->ds != SSEL(SiUDS, SsRPL3) || nur->es != SSEL(SiUDS, SsRPL3)
//	|| nur->fs != SSEL(SiUDS, SsRPL3) || nur->gs != SSEL(SiUDS, SsRPL3)
	){
		qunlock(&up->debug);
		pprint("suicide: bad segment selector in noted\n");
		pexit("Suicide", 0);
	}

	/* don't let user change system flags */
	nur->flags &= (Of|Df|Sf|Zf|Af|Pf|Cf);
	nur->flags |= cur->flags & ~(Of|Df|Sf|Zf|Af|Pf|Cf);

	jehanne_memmove(cur, nur, sizeof(Ureg));

	switch((int)arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(nur->ip, BY2SE, 0) || !okaddr(nur->sp, BY2SE, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted pc=%#p sp=%#p\n",
				nur->ip, nur->sp);
			pexit("Suicide", 0);
		}
		up->ureg = nf->old;
		qunlock(&up->debug);
		break;
	case NSAVE:
		if(!okaddr(nur->ip, BY2SE, 0) || !okaddr(nur->sp, BY2SE, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted pc=%#p sp=%#p\n",
				nur->ip, nur->sp);
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);

		splhi();
		nf->arg1 = nf->msg;
		nf->arg0 = &nf->ureg;
		cur->bp = PTR2UINT(nf->arg0);
		nf->ip = 0;
		cur->sp = PTR2UINT(nf);
		break;
	default:
		up->lastnote.flag = NDebug;
		/* fall through */
	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote.flag == NDebug)
			pprint("suicide: %s\n", up->lastnote.msg);
		pexit(up->lastnote.msg, up->lastnote.flag != NDebug);
		break;
	}
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
{
	int l;
	Mreg s;
	Note note;
	uintptr_t sp;
	NFrame *nf;

	if(up->procctl)
		procctl(up);
	if(up->nnote == 0)
		return 0;

	if(up->fpstate == FPactive){
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	}
	up->fpstate |= FPillegal;

	s = spllo();
	qlock(&up->debug);

	up->notepending = 0;
	up->notedeferred = 0;
	jehanne_memmove(&note, &up->note[0], sizeof(Note));
	if(jehanne_strncmp(note.msg, "sys:", 4) == 0){
		l = jehanne_strlen(note.msg);
		if(l > ERRMAX-sizeof(" pc=0x0123456789abcdef"))
			l = ERRMAX-sizeof(" pc=0x0123456789abcdef");
		jehanne_sprint(note.msg+l, " pc=%#p", ureg->ip);
	}

	if(note.flag != NUser && (up->notified || up->notify == nil)){
		qunlock(&up->debug);
		if(note.flag == NDebug)
			pprint("suicide: %s\n", note.msg);
		pexit(note.msg, note.flag != NDebug);
	}

	if(up->notified){
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	if(up->notify == nil){
		qunlock(&up->debug);
		pexit(note.msg, note.flag != NDebug);
	}
	if(!okaddr(PTR2UINT(up->notify), sizeof(ureg->ip), 0)){
		qunlock(&up->debug);
		pprint("suicide: bad function address %#p in notify\n",
			up->notify);
		pexit("Suicide", 0);
	}

	sp = ureg->sp - sizeof(NFrame);
	if(!okaddr(sp, sizeof(NFrame), 1)){
		qunlock(&up->debug);
		pprint("suicide: bad stack address %#p in notify\n", sp);
		pexit("Suicide", 0);
	}

	nf = UINT2PTR(sp);
	jehanne_memmove(&nf->ureg, ureg, sizeof(Ureg));
	nf->old = up->ureg;
	up->ureg = nf;
	jehanne_memmove(nf->msg, note.msg, ERRMAX);
	nf->arg1 = nf->msg;
	nf->arg0 = &nf->ureg;
	ureg->di = (uintptr_t)nf->arg0;
	ureg->si = (uintptr_t)nf->arg1;
	ureg->bp = PTR2UINT(nf->arg0);
	nf->ip = 0;

	ureg->sp = sp;
	ureg->ip = PTR2UINT(up->notify);
	up->notified = 1;
	up->nnote--;
	jehanne_memmove(&up->lastnote, &note, sizeof(Note));
	jehanne_memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

	qunlock(&up->debug);
	splx(s);

	return 1;
}

void
syscall(Syscalls scallnr, Ureg* ureg)
{
	char *tmp;
	uintptr_t	sp;
	int i, s;
	char *str;
	int64_t startns, stopns;
	ScRet retv;
	void (*pt)(Proc*, int, int64_t, int64_t);

	if(!userureg(ureg))
		panic("syscall: cs %#llux\n", ureg->cs);

	cycles(&up->kentry);

	m->syscall++;
	up->inkernel = 1;
	up->cursyscall = (Syscalls)scallnr;
	up->pc = ureg->ip;
	up->dbgreg = ureg;
	if(up->trace && (pt = proctrace) != nil)
		pt(up, STrap, todget(nil), STrapSC|scallnr);

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		procctl(up);
	}

	up->scallnr = scallnr;
	spllo();

	startns = 0;
	sp = ureg->sp;
	up->nerrlab = 0;
	retv = default_syscall_ret(scallnr);
	if(!waserror()){
		tmp = syscall_name(scallnr);
		if(tmp == nil){
			pprint("bad sys call number %d pc %#llux\n", scallnr, ureg->ip);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}
		if(sp < (USTKTOP-PGSZ) || sp > (USTKTOP-sizeof(up->arg)-BY2SE))
			validaddr(UINT2PTR(sp), sizeof(up->arg)+BY2SE, 0);

		jehanne_memmove(up->arg, UINT2PTR(sp+BY2SE), sizeof(up->arg));
		up->psstate = tmp;

		if(printallsyscalls || up->syscallq != nil){
			str = syscallfmt(scallnr, ureg);
			if(printallsyscalls){
				jehanne_print("%s\n", str);
			}
			if(up->syscallq != nil){
				qlock(&up->debug);
				if(up->syscallq != nil){
					notedefer();
					if(!waserror()){
						qwrite(up->syscallq, str, jehanne_strlen(str));
						poperror();
					}
					noteallow();
				}
				qunlock(&up->debug);
			}
			jehanne_free(str);
			startns = todget(nil);
		}
		dispatch_syscall(scallnr, ureg, &retv);
		poperror();
	} else {
		/* failure: save the error buffer for errstr */
		retv.l = up->syscallerr;
		tmp = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = tmp;
		if(DBGFLG && up->pid == 1)
			iprint("%s: syscall %s error %s\n",
				up->text, syscall_name(scallnr), up->syserrstr);
	}
	if(up->nerrlab){
		jehanne_print("bad errstack [%d]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			jehanne_print("sp=%#p pc=%#p\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	/*
	 * Put return value in frame.
	 * Which element of ScRet to use is based on specific
	 * knowledge of the architecture.
	 */
	ureg->ax = retv.p;
	if(printallsyscalls || up->syscallq != nil){
		stopns = todget(nil);
		str = sysretfmt(scallnr, ureg, &retv, startns, stopns);
		if(printallsyscalls){
			jehanne_print("%s\n", str);
		}
		if(up->syscallq != nil){
			qlock(&up->debug);
			if(up->syscallq != nil
			&& !waserror()){
				notedefer();
				if(!waserror()){
					qwrite(up->syscallq, str, jehanne_strlen(str));
					poperror();
				}
				noteallow();
				poperror();
			}
			qunlock(&up->debug);
		}
		jehanne_free(str);
	}

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		s = splhi();
		procctl(up);
		splx(s);
	}

	up->inkernel = 0;
	up->psstate = 0;
	up->cursyscall = 0;

	if(scallnr == SysNoted)
		noted(ureg, ureg->di);

	splhi();
	if(scallnr != SysRfork && (up->procctl || up->nnote))
		notify(ureg);
	if(up->nnote == 0)
		awake_awakened(up);	// we are not sleeping after all!

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}
	kexit(ureg);
}

uintptr_t
sysexecstack(uintptr_t stack, int argc)
{
	uintptr_t sp;
	/*
	 * Given a current bottom-of-stack that have spaces for a count
	 * of pointer arguments that will be written it and for
	 * an integer argument count, return a suitably
	 * aligned new bottom-of-stack which will satisfy any
	 * hardware stack-alignment contraints.
	 * Rounding the stack down to be aligned with the
	 * natural size of a pointer variable usually suffices,
	 * but some architectures impose further restrictions,
	 * e.g. 32-bit SPARC, where the stack must be 8-byte
	 * aligned although pointers and integers are 32-bits.
	 */

	sp = STACKALIGN(stack);
	/* but we need to align the stack to 16 bytes, not 8
	 */
	sp -= sp & 8 ? 8 : 0;
	//jehanne_print("For %d args, sp is now %p\n", argc, sp);
	return sp;
}

void*
sysexecregs(uintptr_t entry, uint32_t ssize)
{
	uintptr_t *sp;
	Ureg *ureg;

	// We made sure it was correctly aligned in sysexecstack, above.
	if (ssize & 0xf) {
		jehanne_print("your stack is wrong: stacksize is not 16-byte aligned: %d\n", ssize);
		panic("misaligned stack in sysexecregs");
	}
	sp = (uintptr_t*)(USTKTOP - ssize);

	ureg = up->dbgreg;
	ureg->sp = PTR2UINT(sp);
	ureg->ip = entry;
	ureg->cs = UESEL;
	ureg->ss = UDSEL;
	ureg->r14 = ureg->r15 = 0;	/* extern user registers */
	ureg->r12 = up->pid;

	/*
	 * return the address of kernel/user shared data
	 * (e.g. clock stuff)
	 */
	return UINT2PTR(USTKTOP);
}

void
sysprocsetup(Proc* p)
{
	fpprocsetup(p);
	cycles(&p->kentry);
	p->pcycles = -p->kentry;
}

void
sysrforkchild(Proc* child, Proc* parent)
{
	Ureg *cureg;

// In Forsyth's 9k kernel STACKPAD was 3.
// We use 1 to match the l64vsyscall.S we imported from Harvey.
#define STACKPAD 1 /* for return PC? */

	/*
	 * Add STACKPAD*BY2SE to the stack to account for
	 *  - the return PC
	 *  (NOT NOW) - trap's arguments (syscallnr, ureg)
	 */
	child->sched.sp = PTR2UINT(child->kstack+KSTACK-((sizeof(Ureg)+STACKPAD*BY2SE)));
	child->sched.pc = PTR2UINT(sysrforkret);

	cureg = (Ureg*)(child->sched.sp+STACKPAD*BY2SE);
	jehanne_memmove(cureg, parent->dbgreg, sizeof(Ureg));

	cureg->ax = 0;

	/* Things from bottom of syscall which were never executed */
	child->psstate = 0;
	child->inkernel = 0;
}
