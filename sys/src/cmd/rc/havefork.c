/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "rc.h"
#include "getflags.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

void
Xasync(void)
{
	int null = sys_open("/dev/null", OREAD);
	int pid;
	char npid[10];

	if(null<0){
		Xerror("Can't open /dev/null\n");
		return;
	}
	Updenv();
	switch(pid = sys_rfork(RFFDG|RFPROC|RFNOTEG)){
	case -1:
		sys_close(null);
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		pushredir(ROPEN, null, 0);
		start(runq->code, runq->pc+1, runq->local);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		sys_close(null);
		runq->pc = runq->code[runq->pc].i;
		inttoascii(npid, pid);
		setvar(ENV_APID, newword(npid, (word *)0));
		break;
	}
}

void
Xpipe(void)
{
	struct thread *p = runq;
	int pc = p->pc, forkid;
	int lfd = p->code[pc++].i;
	int rfd = p->code[pc++].i;
	int pfd[2];

	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	Updenv();
	switch(forkid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, pc+2, runq->local);
		runq->ret = 0;
		sys_close(pfd[PRD]);
		pushredir(ROPEN, pfd[PWR], lfd);
		break;
	default:
		addwaitpid(forkid);
		start(p->code, p->code[pc].i, runq->local);
		sys_close(pfd[PWR]);
		pushredir(ROPEN, pfd[PRD], rfd);
		p->pc = p->code[pc+1].i;
		p->pid = forkid;
		break;
	}
}

/*
 * Who should wait for the exit from the fork?
 */
enum { Stralloc = 100, };

void
Xbackq(void)
{
	int c, l, pid;
	int pfd[2];
	char *s, *wd, *ewd, *stop;
	struct io *f;
	word *v, *nextv;

	stop = "";
	if(runq->argv && runq->argv->words)
		stop = runq->argv->words->word;
	if(pipe(pfd)<0){
		Xerror("can't make pipe");
		return;
	}
	Updenv();
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		sys_close(pfd[PRD]);
		sys_close(pfd[PWR]);
		return;
	case 0:
		clearwaitpids();
		sys_close(pfd[PRD]);
		start(runq->code, runq->pc+1, runq->local);
		pushredir(ROPEN, pfd[PWR], 1);
		return;
	default:
		addwaitpid(pid);
		sys_close(pfd[PWR]);
		f = openfd(pfd[PRD]);
		s = wd = ewd = 0;
		v = 0;
		while((c = rchr(f))!=EOF){
			if(s==ewd){
				l = s-wd;
				wd = erealloc(wd, l+Stralloc);
				ewd = wd+l+Stralloc-1;
				s = wd+l;
			}
			if(strchr(stop, c)){
				if(s!=wd){
					*s='\0';
					v = newword(wd, v);
					s = wd;
				}
			}
			else *s++=c;
		}
		if(s!=wd){
			*s='\0';
			v = newword(wd, v);
		}
		free(wd);
		closeio(f);
		Waitfor(pid, 0);
		poplist();	/* ditch split in "stop" */
		/* v points to reversed arglist -- reverse it onto argv */
		while(v){
			nextv = v->next;
			v->next = runq->argv->words;
			runq->argv->words = v;
			v = nextv;
		}
		runq->pc = runq->code[runq->pc].i;
		return;
	}
}

void
Xpipefd(void)
{
	struct thread *p = runq;
	int pc = p->pc, pid;
	char name[40];
	int pfd[2];
	int sidefd, mainfd;

	if(pipe(pfd)<0){
		Xerror("can't get pipe");
		return;
	}
	if(p->code[pc].i==READ){
		sidefd = pfd[PWR];
		mainfd = pfd[PRD];
	}
	else{
		sidefd = pfd[PRD];
		mainfd = pfd[PWR];
	}
	Updenv();
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(p->code, pc+2, runq->local);
		sys_close(mainfd);
		pushredir(ROPEN, sidefd, p->code[pc].i==READ?1:0);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		sys_close(sidefd);
		pushredir(ROPEN, mainfd, mainfd);	/* isn't this a noop? */
		strcpy(name, Fdprefix);
		inttoascii(name+strlen(name), mainfd);
		pushword(name);
		p->pc = p->code[pc+1].i;
		break;
	}
}

void
Xsubshell(void)
{
	int pid;

	Updenv();
	switch(pid = fork()){
	case -1:
		Xerror("try again");
		break;
	case 0:
		clearwaitpids();
		start(runq->code, runq->pc+1, runq->local);
		runq->ret = 0;
		break;
	default:
		addwaitpid(pid);
		Waitfor(pid, 1);
		runq->pc = runq->code[runq->pc].i;
		break;
	}
}

int
execforkexec(void)
{
	int pid;
	int n;
	char buf[ERRMAX];

	switch(pid = fork()){
	case -1:
		return -1;
	case 0:
		clearwaitpids();
		pushword("exec");
		execexec();
		strcpy(buf, "can't exec: ");
		n = strlen(buf);
		sys_errstr(buf+n, ERRMAX-n);
		Exit(buf);
	}
	addwaitpid(pid);
	return pid;
}
