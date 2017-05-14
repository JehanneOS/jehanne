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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<ptrace.h>

int
sysrfork(uint32_t flag)
{
	Proc *p;
	int i, pid;
	ProcSegment *s;
	Fgrp *ofg;
	Pgrp *opg;
	Rgrp *org;
	Egrp *oeg;
	Mach *wm;
	uintptr_t ds;
	void (*pt)(Proc*, int, int64_t, int64_t);
	uint64_t ptarg;

	/* Check flags before we commit */
	if((flag & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		error(Ebadarg);
	if((flag & (RFNAMEG|RFCNAMEG)) == (RFNAMEG|RFCNAMEG))
		error(Ebadarg);
	if((flag & (RFENVG|RFCENVG)) == (RFENVG|RFCENVG))
		error(Ebadarg);

	if((flag&RFPROC) == 0) {
		if(flag & (RFMEM|RFNOWAIT))
			error(Ebadarg);
		if(flag & (RFFDG|RFCFDG)) {
			ofg = up->fgrp;
			if(flag & RFFDG)
				up->fgrp = dupfgrp(ofg);
			else
				up->fgrp = dupfgrp(nil);
			closefgrp(ofg);
		}
		if(flag & (RFNAMEG|RFCNAMEG)) {
			opg = up->pgrp;
			up->pgrp = newpgrp();
			if(flag & RFNAMEG)
				pgrpcpy(up->pgrp, opg);
			/* inherit noattach */
			up->pgrp->noattach = opg->noattach;
			closepgrp(opg);
		}
		if(flag & RFNOMNT)
			up->pgrp->noattach = 1;
		if(flag & RFREND) {
			org = up->rgrp;
			up->rgrp = newrgrp();
			closergrp(org);
		}
		if(flag & (RFENVG|RFCENVG)) {
			oeg = up->egrp;
			up->egrp = smalloc(sizeof(Egrp));
			up->egrp->r.ref = 1;
			if(flag & RFENVG)
				envcpy(up->egrp, oeg);
			closeegrp(oeg);
		}
		if(flag & RFNOTEG)
			up->noteid = incref(&noteidalloc);

		return 0;
	}

	if((flag & RFMEM) == 0){
		/* assume half might change copy-on-write, but cap it */
		ds = procdatasize(up, 1)/2;
		if(ds > 64*MB)
			ds = 64*MB;
		if(!umem_available(ds))
			error(Enovmem);
	}

	p = newproc();

	p->trace = up->trace;
	p->scallnr = up->scallnr;
	jehanne_memmove(p->arg, up->arg, sizeof(up->arg));
	p->nerrlab = 0;
	p->slash = up->slash;
	p->dot = up->dot;
	incref(&p->dot->r);

	jehanne_memmove(p->note, up->note, sizeof(p->note));
	p->privatemem = up->privatemem;
	p->nnote = up->nnote;
	p->notified = 0;
	p->lastnote = up->lastnote;
	p->notify = up->notify;
	p->ureg = up->ureg;
	p->dbgreg = 0;

	/* Make a new set of memory segments */
	i = -1;
	MLG("flag %d: RFMEM %d", flag, flag&RFMEM);
	rlock(&up->seglock);
	if(waserror()){
		while(i >= 0){
			if(p->seg[i]){
				segment_release(&p->seg[i]);
			}
			--i;
		}
		jehanne_memset(p->seg, 0, NSEG*sizeof(ProcSegment*));
		runlock(&up->seglock);
		nexterror();
	}
	jehanne_memmove(p->seg, up->seg, NSEG*sizeof(ProcSegment*));
	if(flag & RFMEM){
		for(i = 0; i < NSEG; i++){
			s = p->seg[i];
			if(s && !segment_share(&p->seg[i])){
				--i;	/* p->seg[i] was not allocated, do not release */
				error(Enovmem);
			}
		}
	} else {
		for(i = 0; i < NSEG; i++){
			s = p->seg[i];
			if(s && !segment_fork(&p->seg[i])){
				--i;	/* p->seg[i] was not allocated, do not release */
				error(Enovmem);
			}
		}
	}
	poperror();
	runlock(&up->seglock);

	/* File descriptors */
	if(flag & (RFFDG|RFCFDG)) {
		if(flag & RFFDG)
			p->fgrp = dupfgrp(up->fgrp);
		else
			p->fgrp = dupfgrp(nil);
	}
	else {
		p->fgrp = up->fgrp;
		incref(&p->fgrp->r);
	}

	/* Process groups */
	if(flag & (RFNAMEG|RFCNAMEG)) {
		p->pgrp = newpgrp();
		if(flag & RFNAMEG)
			pgrpcpy(p->pgrp, up->pgrp);
		/* inherit noattach */
		p->pgrp->noattach = up->pgrp->noattach;
	}
	else {
		p->pgrp = up->pgrp;
		incref(&p->pgrp->r);
	}
	if(flag & RFNOMNT)
		p->pgrp->noattach = 1;

	if(flag & RFREND)
		p->rgrp = newrgrp();
	else {
		incref(&up->rgrp->r);
		p->rgrp = up->rgrp;
	}

	/* Environment group */
	if(flag & (RFENVG|RFCENVG)) {
		p->egrp = smalloc(sizeof(Egrp));
		p->egrp->r.ref = 1;
		if(flag & RFENVG)
			envcpy(p->egrp, up->egrp);
	}
	else {
		p->egrp = up->egrp;
		incref(&p->egrp->r);
	}
	p->hang = up->hang;
	p->procmode = up->procmode;

	/* Craft a return frame which will cause the child to pop out of
	 * the scheduler in user mode with the return register zero
	 */
	sysrforkchild(p, up);

	p->parent = up;
	p->parentpid = up->pid;
	if(flag&RFNOWAIT)
		p->parentpid = 0;
	else {
		lock(&up->exl);
		up->nchild++;
		unlock(&up->exl);
	}
	if((flag&RFNOTEG) == 0)
		p->noteid = up->noteid;

	pid = p->pid;
	jehanne_memset(p->time, 0, sizeof(p->time));
	p->time[TReal] = sys->ticks;

	kstrdup(&p->text, up->text);
	kstrdup(&p->user, up->user);
	/*
	 *  since the bss/data segments are now shareable,
	 *  any mmu info about this process is now stale
	 *  (i.e. has bad properties) and has to be discarded.
	 */
	mmuflush();
	p->basepri = up->basepri;
	p->priority = up->basepri;
	p->fixedpri = up->fixedpri;
	p->mp = up->mp;
	wm = up->wired;
	if(wm)
		procwired(p, wm->machno);
	if(p->trace && (pt = proctrace) != nil){
		jehanne_strncpy((char*)&ptarg, p->text, sizeof ptarg);
		pt(p, SName, 0, ptarg);
	}
	ready(p);
	sched();

	return pid;
}

uintptr_t
sysexec(char* p, char **argv)
{
	Ldseg *ldseg, *txtseg, *dataseg;
	Fgrp *f;
	Chan *chan;
	ImagePointer img;
	ElfSegPointer load_segments[NLOAD];
	ProcSegment *s, *ts, *ds, *bs, *es;
	PagePointer page, argvpage;
	int argc, progargc, i, j, n, nldseg;
	char *a, **argvcopy, *elem, *file;
	char line[64], *progarg[sizeof(line)/2+1];
	long hdrsz;
	uintptr_t entry, stack, sbottom, argsize, tmp;
	void (*pt)(Proc*, int, int64_t, int64_t);
	uint64_t ptarg;

	/*
	 * Open the file, remembering the final element and the full name.
	 */
	elem = nil;
	p = validaddr(p, 1, 0);
	file = validnamedup(p, 1);
	MLG("file %d", file);
	if(waserror()){
		jehanne_free(file);
		nexterror();
	}
	chan = namec(file, Aopen, OEXEC, 0);
	if(waserror()){
		if(chan)
			cclose(chan);
		if(elem != nil)
			jehanne_free(elem);
		nexterror();
	}
	kstrdup(&elem, up->genbuf);

	/*
	 * Read the header.
	 * If it's a #!, fill in progarg[] with info then read a new header
	 * from the file indicated by the #!.
	 * The #! line must be less than sizeof(Exec) in size,
	 * including the terminating \n.
	 */
	hdrsz = chan->dev->read(chan, &line, sizeof(line), 0);
	if(hdrsz < 2)
		error(Ebadexec);
	argc = 0;
	progargc = 0;
	if(line[0] == '#' && line[1] == '!'){
		p = jehanne_memchr(line, '\n', MIN(sizeof line, hdrsz));
		if(p == nil)
			error(Ebadexec);
		*p = '\0';
		argc = jehanne_tokenize(line+2, progarg, nelem(progarg));
		if(argc == 0)
			error(Ebadexec);

		/* The original file becomes an extra arg after #! line */
		progarg[argc++] = file;
		progargc = argc;

		/*
		 * Take the #! $0 as a file to open, and replace
		 * $0 with the original path's name.
		 */
		p = progarg[0];
		progarg[0] = elem;
		cclose(chan);

		chan = nil; /* if the following namec() call fails,
		             * the previous waserror() will close(chan):
		             * this assignment let it skip the close,
		             * since chan has just been closed.
		             */
		chan = namec(p, Aopen, OEXEC, 0);
	}

	/*
	 * #! has had its chance, now we need a real binary.
	 */
	nldseg = elf64ldseg(chan, &entry, &ldseg, cputype, PGSZ);
	if(nldseg != 2){
//		jehanne_print("exec: elf64ldseg returned %d segs!\n", nldseg);
		error(Ebadexec);
	}

	txtseg = ldseg;
	dataseg = ldseg+1;


	/*
	 * The new stack will contain, in descending address order:
	 * - argument strings;
	 * - array of pointers to the argument strings with a
	 *   terminating nil (argv).
	 * - argc
	 * When the exec is committed, this temporary stack in es will
	 * become SSEG.
	 * The architecture-dependent code which jumps to the new image
	 * will also push a count of the argument array onto the stack (argc).
	 */
	es = nil;	/* exec new stack */
	if(!segment_virtual(&es, SgStack, SgRead|SgWrite, 0, USTKTOP-USTKSIZE, USTKTOP))
		error(Enovmem);
	if(waserror()){
		segment_release(&es);
		nexterror();
	}

	/* Step 0: Compute the total size and number of arguments */
	argsize = 0;

	/* 	start with arguments found from a #! header. */
	for(i = 0; i < argc; i++)
		argsize += jehanne_strlen(progarg[i]) + 1 + sizeof(char*);

	/* 	then size strings pointed to by the syscall argument
	 * 	argv verifing that both argv and the strings it
	 * 	points to are valid.
	 */
	argvcopy = argv;
	evenaddr(PTR2UINT(argvcopy));
	for(i = 0;; i++, argvcopy++){
		a = *(char**)validaddr(argvcopy, sizeof(char**), 0);
		if(a == nil)
			break;
		a = validaddr(a, 1, 0);
		n = ((char*)vmemchr(a, 0, 0x7fffffff) - a) + 1;

		/* This futzing is so argv[0] gets validated even
		 * though it will be thrown away if this is a shell
		 * script.
		 */
		if(argc > 0 && i == 0)
			continue;
		argsize += n + sizeof(char*);
//jehanne_print("argv[%d] = %s, argsize %d, n %d\n", argc, a, argsize, n);
		argc++;
	}
	if(argc < 1)
		error(Ebadexec);
	argsize += sizeof(char*);	/* place for argv[argc] = nil */
	argsize += sizeof(uintptr_t);	/* place for argc = nil */

	tmp = es->top - argsize;
	if(tmp&~(PGSZ-1) != (tmp+sizeof(uintptr_t)+sizeof(char*)*(argc+1))&~(PGSZ-1)){
		/* the argument pointers cross a page boundary, keep
		 * them all in the same page
		 */
		tmp -= (tmp+sizeof(uintptr_t)+sizeof(char*)*(argc+1))&(PGSZ-1);
	}
	tmp = sysexecstack(tmp, argc);

	argsize += es->top - argsize - tmp;

	/* Step 1: Fault enough pages in the new stack */
	stack = es->top;
	while(stack > es->top - argsize){
		stack -= PGSZ;
		if(!segment_fault(&tmp, &stack, es, FaultWrite))
			error(Enovmem);
	}

//jehanne_print("argsize %d, first stack page %d\n", argsize, stack);

	/* Step 2: Copy arguments into pages in descending order */

	/*	prepare argvcopy to point to the right location */
	tmp = es->top - argsize;
	argvpage = segment_page(es, tmp);
	char *apmem;
	if(argvpage == 0)
		panic("sysexec: segment_fault did not allocate enough pages");
	apmem = page_kmap(argvpage);
	argvcopy = (char**)((uintptr_t)(apmem + (tmp&(PGSZ-1))));

	/*	add argc */
	*((uintptr_t*)argvcopy) = argc;
	++argvcopy;

	/*	prepare pmem to to point to the last stack page */
	char *pmem;
	stack = es->top;
	sbottom = es->top;
	sbottom -= PGSZ;
	page = segment_page(es, sbottom);
	if(page == 0)
		panic("sysexec: segment_fault did not allocate enough pages");
	pmem = page_kmap(page);

	/* 	start filling pmem (from the end) and argvcopy
	 * 	(from the begin) with arguments found
	 * 	from a #! header.
	 */
	for(i = 0; i < progargc; i++){
		n = jehanne_strlen(progarg[i])+1;
CopyProgArgument:
		if(sbottom <= stack-n){
			a = pmem+((stack-n)&(PGSZ-1));
			jehanne_memmove(a, progarg[i], n);
			stack -= n;
		} else {
			/* the current argument cross multiple pages */
			if(stack&(PGSZ-1)){
				/* fill the rest of the current page */
				jehanne_memmove(pmem, progarg[i]+n-1-(stack&(PGSZ-1)), (stack&(PGSZ-1))-1);
				*(pmem+(stack&(PGSZ-1))-1) = 0;
				n -= (stack&(PGSZ-1));
				stack -= (stack&(PGSZ-1));
			}
			while(sbottom > stack - n){
				page_kunmap(page, &pmem);
				sbottom -= PGSZ;
				page = segment_page(es, sbottom);
				if(page == 0)
					panic("sysexec: segment_fault did not allocate enough pages");
				pmem = page_kmap(page);
				if(n > PGSZ){
					/* fill one full page */
					jehanne_memmove(pmem, progarg[i]+n-PGSZ, PGSZ);
					n -= PGSZ;
					stack -= PGSZ;
				}
			}
			goto CopyProgArgument;
		}
		argvcopy[i] = (char*)stack;
	}

	if(progargc > 0){
		/* we are in a script: argv[0] has been replaced in
		 * progarg and already copied, so we need to skip
		 * it and add any further elements from argv.
		 */
		--progargc;
	}

	/* 	continue filling pmem (descending) and argvcopy
	 * 	(from the current point) with exec arguments
	 */
	for(; i < argc; i++){
		j = i - progargc;
		n = jehanne_strlen(argv[j])+1;

CopyExecArgument:
		if(sbottom <= stack-n){
			a = pmem+((stack-n)&(PGSZ-1));
			jehanne_memmove(a, argv[j], n);
			stack -= n;
		} else {
			/* the current argument cross multiple pages */
			if(stack&(PGSZ-1)){
				/* fill the rest of the current page */
				jehanne_memmove(pmem, argv[j]+n-1-(stack&(PGSZ-1)), (stack&(PGSZ-1))-1);
				*(pmem+(stack&(PGSZ-1))-1) = 0;
				n -= (stack&(PGSZ-1));
				stack -= (stack&(PGSZ-1));
			}
			while(sbottom > stack - n){
				page_kunmap(page, &pmem);
				sbottom -= PGSZ;
				page = segment_page(es, sbottom);
				if(page == 0)
					panic("sysexec: segment_fault did not allocate enough pages");
				pmem = page_kmap(page);
				if(n > PGSZ){
					/* fill one full page */
					jehanne_memmove(pmem, argv[j]+n-PGSZ, PGSZ);
					n -= PGSZ;
					stack -= PGSZ;
				}
			}
			goto CopyExecArgument;
		}

		argvcopy[i] = (char*)stack;
		INSPECT(stack);
		INSPECT(pmem);
	}

	argvcopy[i] = nil;	/* terminating nil */
	page_kunmap(page, &pmem);
	page_kunmap(argvpage, &apmem);

	INSPECT(argvcopy);

	/*
	 * All the argument processing is now done, ready for the image.
	 */

	/* build image for file */
	if(!image_attach(&img, chan, ldseg))
		error(Enovmem);
	if(waserror()){
		image_release(img);
		nexterror();
	}

	image_segments(load_segments, img);

	ts = nil;
	if(!segment_load(&ts, load_segments[0], txtseg))
		error(Enovmem);
	if(waserror()){
		segment_release(&ts);
		nexterror();
	}

	ds = nil;
	if(!segment_load(&ds, load_segments[1], dataseg))
		error(Enovmem);
	if(waserror()){
		segment_release(&ds);
		nexterror();
	}

	bs = nil;
	tmp = dataseg->pg0vaddr + dataseg->pg0off + dataseg->memsz;
	if(tmp < ds->top)
		tmp = ds->top;
	if(!segment_virtual(&bs, SgBSS, SgRead|SgWrite, 0,
		ds->top,
		tmp))
		error(Enovmem);

	jehanne_free(ldseg);	/* free elf segments */

	/*
	 * Close on exec
	 */
	f = up->fgrp;
	for(i=0; i<=f->maxfd; i++)
		fdclose(i, CCEXEC);

	wlock(&up->seglock);
	if(waserror()){
		wunlock(&up->seglock);
		nexterror();
	}

	/*
	 * Free old memory.
	 * Special segments maintained across exec.
	 */
	for(i = SSEG; i <= BSEG; i++) {
		if(up->seg[i])
			segment_release(&up->seg[i]);
	}
	for(i = BSEG+1; i< NSEG; i++) {
		s = up->seg[i];
		if(s && (s->flags&SgCExec))
			segment_release(&up->seg[i]);
	}

	if(up->trace && (pt = proctrace) != nil){
		jehanne_strncpy((char*)&ptarg, elem, sizeof ptarg);
		pt(up, SName, 0, ptarg);
	}

	/*
	 *  At this point, the mmu contains info about the old address
	 *  space and needs to be flushed
	 */
	mmuflush();

	up->seg[SSEG] = es;
	up->seg[TSEG] = ts;
	up->seg[DSEG] = ds;
	up->seg[BSEG] = bs;
	poperror();	/* ds */
	poperror();	/* ts */
	poperror();	/* es */
	poperror();	/* img */
	image_release(img);

	jehanne_free(up->text);
	up->text = elem;
	elem = nil;
	if(up->setargs)	/* setargs == 0 => args in stack from sysexec */
		jehanne_free(up->args);
	up->args = argvcopy;
	up->nargs = argc;
	up->setargs = 0;

	if(up->parentpid == 0){
		/* this is *init* replacing itself: we set it to 1
		 * so that we can say that all processes started from
		 * an image have a parentpid (up->parent is still 0)
		 * (see segment_release)
		 */
		up->parentpid = 1;
	}

//	poperror();				/* p (up->args) */

	poperror();				/* seglock */
	wunlock(&up->seglock);

	/*
	 *  '/' processes are higher priority. (TO DO: really?)
	 */
	if(chan->dev->dc == L'/')
		up->basepri = PriRoot;
	up->priority = up->basepri;
	poperror();				/* chan, elem */
	cclose(chan);
	poperror();		/* file */
	jehanne_free(file);

	qlock(&up->debug);
	up->nnote = 0;
	up->notify = 0;
	up->notified = 0;
	up->privatemem = 0;
	sysprocsetup(up);
	qunlock(&up->debug);
	if(up->hang)
		up->procctl = Proc_stopme;

	return (uintptr_t)sysexecregs(entry, argsize);
}

int
return0(void* _1)
{
	return 0;
}

long
sysalarm(unsigned long millisecs)
{
	return procalarm(millisecs);
}

long
sysawake(long millisecs)
{
	return procawake(millisecs);
}

int
sys_exits(char *status)
{
	char *inval = "invalid exit string";
	char buf[ERRMAX];

	if(status){
		if(waserror())
			status = inval;
		else{
			status = validaddr(status, 1, 0);
			if(vmemchr(status, 0, ERRMAX) == 0){
				jehanne_memmove(buf, status, ERRMAX);
				buf[ERRMAX-1] = 0;
				status = buf;
			}
			poperror();
		}
	}
	pexit(status, 1);
	return 0;
}

int
sysawait(char *p, int n)
{
	int i;
	int pid;
	Waitmsg w;

	/*
	 * int await(char* s, int n);
	 * should really be
	 * usize await(char* s, usize n);
	 */
	p = validaddr(p, n, 1);

	pid = pwait(&w);
	if(pid < 0)
		return -1;

	i = jehanne_snprint(p, n, "%d %lud %lud %lud %q",
		w.pid,
		w.time[TUser], w.time[TSys], w.time[TReal],
		w.msg);

	return i;
}

void
jehanne_werrstr(char *fmt, ...)
{
	va_list va;

	if(up == nil)
		return;

	va_start(va, fmt);
	jehanne_vseprint(up->syserrstr, up->syserrstr+ERRMAX, fmt, va);
	va_end(va);
}

static void
generrstr(char *buf, int n)
{
	char *p, tmp[ERRMAX];

	if(n <= 0)
		error(Ebadarg);
	p = validaddr(buf, n, 1);
	if(n > sizeof tmp)
		n = sizeof tmp;
	jehanne_memmove(tmp, p, n);

	/* make sure it's NUL-terminated */
	tmp[n-1] = '\0';
	jehanne_memmove(p, up->syserrstr, n);
	p[n-1] = '\0';
	jehanne_memmove(up->syserrstr, tmp, n);
}

int
syserrstr(char* err, int nerr)
{
	generrstr(err, nerr);

	return 0;
}

int
sysnotify(void* a0)
{
	void (*f)(void*, char*);

	/*
	 * int notify(void (*f)(void*, char*));
	 */
	f = (void (*)(void*, char*))a0;

	if(f != nil)
		validaddr(f, sizeof(void (*)(void*, char*)), 0);
	up->notify = f;

	return 0;
}

int
sysnoted(int v)
{
	if(v != NRSTR && !up->notified)
		error(Egreg);

	return 0;
}

void*
sysrendezvous(void* tagp, void* rendvalp)
{
	Proc *p, **l;
	uintptr_t tag, val, pc, rendval;
	void (*pt)(Proc*, int, int64_t, int64_t);
	void *result;

	tag = PTR2UINT(tagp);
	rendval = PTR2UINT(rendvalp);

	l = &REND(up->rgrp, tag);
	up->rendval = ~0;

	lock(&up->rgrp->l);
	for(p = *l; p; p = p->rendhash) {
		if(p->rendtag == tag) {
			*l = p->rendhash;
			val = p->rendval;
			p->rendval = rendval;
			unlock(&up->rgrp->l);

			while(p->mach != 0)
				;
			ready(p);

			result = UINT2PTR(val);

			goto rendezvousDone;
		}
		l = &p->rendhash;
	}

	if(awakeOnBlock(up)){
		unlock(&up->rgrp->l);
		result = UINT2PTR(up->rendval);
		goto rendezvousDone;
	}
	/* Going to sleep here */
	up->rendtag = tag;
	up->rendval = rendval;
	up->rendhash = *l;
	*l = up;
	up->state = Rendezvous;
	if(up->trace && (pt = proctrace) != nil){
		pc = (uintptr_t)sysrendezvous;
		pt(up, SSleep, 0, Rendezvous|(pc<<8));
	}
	unlock(&up->rgrp->l);

	sched();

	result = UINT2PTR(up->rendval);
rendezvousDone:
	return result;
}

/*
 * The implementation of semaphores is complicated by needing
 * to avoid rescheduling in syssemrelease, so that it is safe
 * to call from real-time processes.  This means syssemrelease
 * cannot acquire any qlocks, only spin locks.
 *
 * Semacquire and semrelease must both manipulate the semaphore
 * wait list.  Lock-free linked lists only exist in theory, not
 * in practice, so the wait list is protected by a spin lock.
 *
 * The semaphore value *addr is stored in user memory, so it
 * cannot be read or written while holding spin locks.
 *
 * Thus, we can access the list only when holding the lock, and
 * we can access the semaphore only when not holding the lock.
 * This makes things interesting.  Note that sleep's condition function
 * is called while holding two locks - r and up->rlock - so it cannot
 * access the semaphore value either.
 *
 * An acquirer announces its intention to try for the semaphore
 * by putting a Sema structure onto the wait list and then
 * setting Sema.waiting.  After one last check of semaphore,
 * the acquirer sleeps until Sema.waiting==0.  A releaser of n
 * must wake up n acquirers who have Sema.waiting set.  It does
 * this by clearing Sema.waiting and then calling wakeup.
 *
 * There are three interesting races here.

 * The first is that in this particular sleep/wakeup usage, a single
 * wakeup can rouse a process from two consecutive sleeps!
 * The ordering is:
 *
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) set Sema.waiting = 0
 * 	(a) check Sema.waiting inside sleep, return w/o sleeping
 * 	(a) try for semaphore, fail
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) call wakeup(a)
 * 	(a) wake up again
 *
 * This is okay - semacquire will just go around the loop
 * again.  It does mean that at the top of the for(;;) loop in
 * semacquire, phore.waiting might already be set to 1.
 *
 * The second is that a releaser might wake an acquirer who is
 * interrupted before he can acquire the lock.  Since
 * release(n) issues only n wakeup calls -- only n can be used
 * anyway -- if the interrupted process is not going to use his
 * wakeup call he must pass it on to another acquirer.
 *
 * The third race is similar to the second but more subtle.  An
 * acquirer sets waiting=1 and then does a final canacquire()
 * before going to sleep.  The opposite order would result in
 * missing wakeups that happen between canacquire and
 * waiting=1.  (In fact, the whole point of Sema.waiting is to
 * avoid missing wakeups between canacquire() and sleep().) But
 * there can be spurious wakeups between a successful
 * canacquire() and the following semdequeue().  This wakeup is
 * not useful to the acquirer, since he has already acquired
 * the semaphore.  Like in the previous case, though, the
 * acquirer must pass the wakeup call along.
 *
 * This is all rather subtle.  The code below has been verified
 * with the spin model /sys/src/9/port/semaphore.p.  The
 * original code anticipated the second race but not the first
 * or third, which were caught only with spin.  The first race
 * is mentioned in /sys/doc/sleep.ps, but I'd forgotten about it.
 * It was lucky that my abstract model of sleep/wakeup still managed
 * to preserve that behavior.
 *
 * I remain slightly concerned about memory coherence
 * outside of locks.  The spin model does not take
 * queued processor writes into account so we have to
 * think hard.  The only variables accessed outside locks
 * are the semaphore value itself and the boolean flag
 * Sema.waiting.  The value is only accessed with CAS,
 * whose job description includes doing the right thing as
 * far as memory coherence across processors.  That leaves
 * Sema.waiting.  To handle it, we call coherence() before each
 * read and after each write.		- rsc
 */

/* Add semaphore p with addr a to list in seg. */
static void
semqueue(ProcSegment* s, int* addr, Sema* p)
{
	jehanne_memset(p, 0, sizeof *p);
	p->addr = addr;

	lock(&s->sema.rend.l);	/* uses s->sema.Rendez.Lock, but no one else is */
	p->next = &s->sema;
	p->prev = s->sema.prev;
	p->next->prev = p;
	p->prev->next = p;
	unlock(&s->sema.rend.l);
}

/* Remove semaphore p from list in seg. */
static void
semdequeue(ProcSegment* s, Sema* p)
{
	lock(&s->sema.rend.l);
	p->next->prev = p->prev;
	p->prev->next = p->next;
	unlock(&s->sema.rend.l);
}

/* Wake up n waiters with addr on list in seg. */
static void
semwakeup(ProcSegment* s, int* addr, int n)
{
	Sema *p;

	lock(&s->sema.rend.l);
	for(p = s->sema.next; p != &s->sema && n > 0; p = p->next){
		if(p->addr == addr && p->waiting){
			p->waiting = 0;
			coherence();
			wakeup(&p->rend);
			n--;
		}
	}
	unlock(&s->sema.rend.l);
}

/* Add delta to semaphore and wake up waiters as appropriate. */
static int
semrelease(ProcSegment* s, int* addr, int delta)
{
	int value;

	do
		value = *addr;
	while(!CASW(addr, value, value+delta));
	semwakeup(s, addr, delta);

	return value+delta;
}

/* Try to acquire semaphore using compare-and-swap */
static int
canacquire(int* addr)
{
	int value;

	while((value = *addr) > 0){
		if(CASW(addr, value, value-1))
			return 1;
	}

	return 0;
}

/* Should we wake up? */
static int
semawoke(void* p)
{
	coherence();
	return !((Sema*)p)->waiting;
}

/* Acquire semaphore (subtract 1). */
static int
semacquire(ProcSegment* s, int* addr, int block)
{
	int acquired;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(!block)
		return 0;

	acquired = 0;
	semqueue(s, addr, &phore);
	for(;;){
		phore.waiting = 1;
		coherence();
		if(canacquire(addr)){
			acquired = 1;
			break;
		}
		if(waserror())
			break;
		sleep(&phore.rend, semawoke, &phore);
		poperror();
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(!acquired)
		nexterror();

	return 1;
}

int
syssemacquire(int* addr, int block)
{
	ProcSegment *s;

	addr = validaddr(addr, sizeof(int), 1);
	evenaddr(PTR2UINT(addr));

	s = proc_segment(up, PTR2UINT(addr));
	if(s == nil || (s->permissions&SgWrite) == 0 || (uintptr_t)addr+sizeof(int) > s->top){
		validaddr(addr, sizeof(int), 1);
		error(Ebadarg);
	}
	if(*addr < 0)
		error(Ebadarg);

	return semacquire(s, addr, block);
}

int
syssemrelease(int* addr, int delta)
{
	ProcSegment *s;

	addr = validaddr(addr, sizeof(int), 1);
	evenaddr(PTR2UINT(addr));

	s = proc_segment(up, PTR2UINT(addr));
	if(s == nil || (s->permissions&SgWrite) == 0 || (uintptr_t)addr+sizeof(int) > s->top){
		validaddr(addr, sizeof(int), 1);
		error(Ebadarg);
	}
	/* delta == 0 is a no-op, not a release */
	if(delta < 0 || *addr < 0)
		error(Ebadarg);

	return semrelease(s, addr, delta);
}
