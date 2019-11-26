/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

int	_threadnopasser;

#define	NFN		33
#define	ERRLEN	48
typedef struct Note Note;
struct Note
{
	Lock		inuse;
	Proc		*proc;		/* recipient */
	char		s[ERRMAX];	/* arg2 */
};

static Note	notes[128];
static Note	*enotes = notes+nelem(notes);
static int		(*onnote[NFN])(void*, char*);
static int		onnotepid[NFN];
static Lock	onnotelock;

int
threadnotify(int (*f)(void*, char*), int in)
{
	int i, topid;
	int (*from)(void*, char*), (*to)(void*, char*);

	if(in){
		from = nil;
		to = f;
		topid = _threadgetproc()->pid;
	}else{
		from = f;
		to = nil;
		topid = 0;
	}
	jehanne_lock(&onnotelock);
	for(i=0; i<NFN; i++)
		if(onnote[i]==from){
			onnote[i] = to;
			onnotepid[i] = topid;
			break;
		}
	jehanne_unlock(&onnotelock);
	return i<NFN;
}

static void
delayednotes(Proc *p, void *v)
{
	int i;
	Note *n;
	int (*fn)(void*, char*);

	if(!p->pending)
		return;

	p->pending = 0;
	for(n=notes; n<enotes; n++){
		if(n->proc == p){
			for(i=0; i<NFN; i++){
				if(onnotepid[i]!=p->pid || (fn = onnote[i])==nil)
					continue;
				if((*fn)(v, n->s))
					break;
			}
			if(i==NFN){
				_threaddebug(DBGNOTE, "Unhandled note %s, proc %p\n", n->s, p);
				if(v != nil)
					sys_noted(NDFLT);
				else if(jehanne_strncmp(n->s, "sys:", 4)==0)
					abort();
				threadexitsall(n->s);
			}
			n->proc = nil;
			jehanne_unlock(&n->inuse);
		}
	}
}

void
_threadnote(void *v, char *s)
{
	Proc *p;
	Note *n;

	_threaddebug(DBGNOTE, "Got note %s", s);
	if(jehanne_strncmp(s, "sys:", 4) == 0)
		sys_noted(NDFLT);

	if(_threadexitsallstatus){
		_threaddebug(DBGNOTE, "Threadexitsallstatus = '%s'\n", _threadexitsallstatus);
		sys__exits(_threadexitsallstatus);
	}

	if(jehanne_strcmp(s, "threadint")==0)
		sys_noted(NCONT);

	p = _threadgetproc();
	if(p == nil)
		sys_noted(NDFLT);

	for(n=notes; n<enotes; n++)
		if(jehanne_canlock(&n->inuse))
			break;
	if(n==enotes)
		jehanne_sysfatal("libthread: too many delayed notes");
	jehanne_utfecpy(n->s, n->s+ERRMAX, s);
	n->proc = p;
	p->pending = 1;
	if(!p->splhi)
		delayednotes(p, v);
	sys_noted(NCONT);
}

int
_procsplhi(void)
{
	int s;
	Proc *p;

	p = _threadgetproc();
	s = p->splhi;
	p->splhi = 1;
	return s;
}

void
_procsplx(int s)
{
	Proc *p;

	p = _threadgetproc();
	p->splhi = s;
	if(s)
		return;
	if(p->pending)
		delayednotes(p, nil);
}

