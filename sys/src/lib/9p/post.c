/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <lib9.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

static void postproc(void*);

void
_postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	int fd[2];

	if(!s->nopipe){
		if(pipe(fd) < 0)
			sysfatal("pipe: %r");
		s->infd = s->outfd = fd[1];
		s->srvfd = fd[0];
	}
	if(name)
		if(postfd(name, s->srvfd) < 0)
			sysfatal("postfd %s: %r", name);

	if(_forker == nil)
		sysfatal("no forker");
	_forker(postproc, s, RFNAMEG);

	/*
	 * Normally the server is posting as the last thing it does
	 * before exiting, so the correct thing to do is drop into
	 * a different fd space and close the 9P server half of the
	 * pipe before trying to mount the kernel half.  This way,
	 * if the file server dies, we don't have a ref to the 9P server
	 * half of the pipe.  Then killing the other procs will drop
	 * all the refs on the 9P server half, and the mount will fail.
	 * Otherwise the mount hangs forever.
	 *
	 * Libthread in general and acme win in particular make
	 * it hard to make this fd bookkeeping work out properly,
	 * so leaveinfdopen is a flag that win sets to opt out of this
	 * safety net.
	 */
	if(!s->leavefdsopen){
		sys_rfork(RFFDG);
		sys_rendezvous(0, 0);
		sys_close(s->infd);
		if(s->infd != s->outfd)
			sys_close(s->outfd);
	}

	if(mtpt){
		if(amount(s->srvfd, mtpt, flag, "") == -1)
			sysfatal("mount %s: %r", mtpt);
	}else
		sys_close(s->srvfd);
}

void
_postsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	int fd[2];

	if(!s->nopipe){
		if(pipe(fd) < 0)
			sysfatal("pipe: %r");
		s->infd = s->outfd = fd[1];
		s->srvfd = fd[0];
	}
	if(name)
		if(postfd(name, s->srvfd) < 0)
			sysfatal("postfd %s: %r", name);

	if(_forker == nil)
		sysfatal("no forker");
	_forker(postproc, s, RFNAMEG);

	/*
	 * Normally the server is posting as the last thing it does
	 * before exiting, so the correct thing to do is drop into
	 * a different fd space and close the 9P server half of the
	 * pipe before trying to mount the kernel half.  This way,
	 * if the file server dies, we don't have a ref to the 9P server
	 * half of the pipe.  Then killing the other procs will drop
	 * all the refs on the 9P server half, and the mount will fail.
	 * Otherwise the mount hangs forever.
	 *
	 * Libthread in general and acme win in particular make
	 * it hard to make this fd bookkeeping work out properly,
	 * so leaveinfdopen is a flag that win sets to opt out of this
	 * safety net.
	 */
	if(!s->leavefdsopen){
		sys_rfork(RFFDG);
		sys_rendezvous(0, 0);
		sys_close(s->infd);
		if(s->infd != s->outfd)
			sys_close(s->outfd);
	}

	if(mtpt){
		if(sharefd(mtpt, desc, s->srvfd) < 0)
			sysfatal("sharefd %s: %r", mtpt);
	}else
		sys_close(s->srvfd);
}


static void
postproc(void *v)
{
	Srv *s;

	s = v;
	if(!s->leavefdsopen){
		sys_rfork(RFNOTEG);
		sys_rendezvous(0, 0);
		sys_close(s->srvfd);
	}
	srv(s);
}
