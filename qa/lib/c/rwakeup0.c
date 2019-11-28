/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
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
#include <u.h>
#include <lib9.h>

/* verify that rwakeup returns 0 on Rendez that has already been awaken */

Rendez r;
QLock l;
int ready;
int verbose = 0;

int killerProc;	/* pid, will kill the other processes if starved */

int
handletimeout(void *v, char *s)
{
	/* just not exit, please */
	if(strcmp(s, "timedout") == 0){
		if(verbose)
			print("%d: noted: %s\n", getpid(), s);
		print("FAIL: %s timedout\n", argv0);
		exits("FAIL");
	}
	return 0;
}

void
killKiller(void)
{
	postnote(PNPROC, killerProc, "interrupt");
}

void
stopAllAfter(int seconds)
{
	int pid;

	switch((pid = sys_rfork(RFMEM|RFPROC|RFNOWAIT)))
	{
		case 0:
			if(verbose)
				print("killer proc started: pid %d\n", getpid());
			sleep(seconds * 1000);
			postnote(PNGROUP, killerProc, "timedout");
			if(verbose)
				print("killer proc timedout: pid %d\n", getpid());
			exits("FAIL");
		case -1:
			fprint(2, "%r\n");
			exits("sys_rfork fails");
		default:
			killerProc = pid;
			atexit(killKiller);
	}
}

void
main(int argc, char* argv[])
{
	int s, w;

	ARGBEGIN{
	}ARGEND;

	sys_rfork(RFNOTEG|RFREND);
	if (!atnotify(handletimeout, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	r.l = &l;

	stopAllAfter(30);
	/* one process to sleep */
	switch((s = sys_rfork(RFMEM|RFPROC|RFNOWAIT)))
	{
		case 0:
			qlock(&l);
			ready = 1;
			rsleep(&r);
			qunlock(&l);
			exits(nil);
			break;
		case -1:
			print("sys_rfork: %r\n");
			exits("sys_rfork fails");
			break;
		default:
			while(ready == 0)
				;
			break;
	}
	/* one process to wakeup */
	switch((w = sys_rfork(RFMEM|RFPROC|RFNOWAIT)))
	{
		case 0:
			qlock(&l);
			rwakeup(&r);
			qunlock(&l);
			ready = 2;
			exits(nil);
			break;
		case -1:
			print("sys_rfork: %r\n");
			exits("sys_rfork fails");
			break;
		default:
			while(ready == 1)
				;
			break;
	}
	/* now, we try to wakeup a free Rendez */
	qlock(&l);
	if(rwakeup(&r) == 0){
		qunlock(&l);
		print("PASS\n");
		exits("PASS");
	}
	if((s = sys_open(smprint("/proc/%d/ctl", s), OWRITE)) >= 0){
		jehanne_write(s, "kill", 5);
		sys_close(s);
	}
	if((w = sys_open(smprint("/proc/%d/ctl", w), OWRITE)) >= 0){
		jehanne_write(s, "kill", 5);
		sys_close(s);
	}
	print("FAIL\n");
	exits("FAIL");
}
