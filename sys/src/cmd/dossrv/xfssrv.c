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
#include <auth.h>
#include <9P2000.h>
#include "iotrack.h"
#include "dat.h"
#include "dosfs.h"
#include "fns.h"

#include "errstr.h"

#define	Reqsize	(sizeof(Fcall)+Maxfdata)
Fcall	*req;
Fcall	*rep;

uint8_t	mdata[Maxiosize];
char	repdata[Maxfdata];
uint8_t	statbuf[STATMAX];
int	errno;
char	errbuf[ERRMAX];
void	rmservice(void);
char	srvfile[64];
char	*deffile;
int	doabort;
int	trspaces;

void	(*fcalls[])(void) = {
	[Tversion]	rversion,
	[Tflush]	rflush,
	[Tauth]	rauth,
	[Tattach]	rattach,
	[Twalk]		rwalk,
	[Topen]		ropen,
	[Tcreate]	rcreate,
	[Tread]		rread,
	[Twrite]	rwrite,
	[Tclunk]	rclunk,
	[Tremove]	rremove,
	[Tstat]		rstat,
	[Twstat]	rwstat,
};

void
usage(void)
{
	jehanne_fprint(2, "usage: %s [-v] [-s] [-f devicefile] [srvname]\n", argv0);
	jehanne_exits("usage");
}

void
main(int argc, char **argv)
{
	int stdio, srvfd, pipefd[2];

	rep = jehanne_malloc(sizeof(Fcall));
	req = jehanne_malloc(Reqsize);
	if(rep == nil || req == nil)
		panic("out of memory");
	stdio = 0;
	ARGBEGIN{
	case ':':
		trspaces = 1;
		break;
	case 'r':
		readonly = 1;
		break;
	case 'v':
		++chatty;
		break;
	case 'f':
		deffile = ARGF();
		break;
	case 's':
		stdio = 1;
		break;
	case 'p':
		doabort = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		jehanne_strcpy(srvfile, "#s/dos");
	else if(argc == 1)
		jehanne_snprint(srvfile, sizeof srvfile, "#s/%s", argv[0]);
	else
		usage();

	if(stdio){
		pipefd[0] = 0;
		pipefd[1] = 1;
	}else{
		sys_close(0);
		sys_close(1);
		sys_open("/dev/null", OREAD);
		sys_open("/dev/null", OWRITE);
		if(jehanne_pipe(pipefd) < 0)
			panic("pipe");
		srvfd = jehanne_ocreate(srvfile, OWRITE|ORCLOSE, 0600);
		if(srvfd < 0)
			panic(srvfile);
		jehanne_fprint(srvfd, "%d", pipefd[0]);
		sys_close(pipefd[0]);
		jehanne_atexit(rmservice);
		jehanne_fprint(2, "%s: serving %s\n", argv0, srvfile);
	}
	srvfd = pipefd[1];

	switch(sys_rfork(RFNOWAIT|RFNOTEG|RFFDG|RFPROC|RFNAMEG)){
	case -1:
		panic("fork");
	default:
		sys__exits(0);
	case 0:
		break;
	}

	iotrack_init();

	if(!chatty){
		sys_close(2);
		sys_open("#c/cons", OWRITE);
	}

	io(srvfd);
	jehanne_exits(0);
}

void
io(int srvfd)
{
	int n, pid;

	pid = jehanne_getpid();

	jehanne_fmtinstall('F', fcallfmt);
	for(;;){
		/*
		 * reading from a pipe or a network device
		 * will give an error after a few eof reads.
		 * however, we cannot tell the difference
		 * between a zero-length read and an interrupt
		 * on the processes writing to us,
		 * so we wait for the error.
		 */
		n = read9pmsg(srvfd, mdata, sizeof mdata);
		if(n < 0)
			break;
		if(n == 0)
			continue;
		if(convM2S(mdata, n, req) == 0)
			continue;

		if(chatty)
			jehanne_fprint(2, "dossrv %d:<-%F\n", pid, req);

		errno = 0;
		if(!fcalls[req->type])
			errno = Ebadfcall;
		else
			(*fcalls[req->type])();
		if(errno){
			rep->type = Rerror;
			rep->ename = xerrstr(errno);
		}else{
			rep->type = req->type + 1;
			rep->fid = req->fid;
		}
		rep->tag = req->tag;
		if(chatty)
			jehanne_fprint(2, "dossrv %d:->%F\n", pid, rep);
		n = convS2M(rep, mdata, sizeof mdata);
		if(n == 0)
			panic("convS2M error on write");
		if(jehanne_write(srvfd, mdata, n) != n)
			panic("mount write");
	}
	chat("server shut down");
}

void
rmservice(void)
{
	sys_remove(srvfile);
}

char *
xerrstr(int e)
{
	if (e < 0 || e >= sizeof errmsg/sizeof errmsg[0])
		return "no such error";
	if(e == Eerrstr){
		sys_errstr(errbuf, sizeof errbuf);
		return errbuf;
	}
	return errmsg[e];
}

int
eqqid(Qid q1, Qid q2)
{
	return q1.path == q2.path && q1.type == q2.type && q1.vers == q2.vers;
}
