/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <u.h>
#include <lib9.h>
#include <posix.h>
#include "internal.h"


/* pointer to the pid to forward notes to */
extern ChildList **__libposix_child_list;
extern WaitList **__libposix_wait_list;
extern SignalConf *__libposix_signals;
extern int *__libposix_devsignal;

static void
open_sighelper_nanny(void)
{
	int mypid;
	if(*__libposix_devsignal >= 0)
		sys_close(*__libposix_devsignal);
	mypid = *__libposix_pid;
	*__libposix_devsignal = sys_create("/dev/posix/nanny", OWRITE|OCEXEC, mypid);
	if(*__libposix_devsignal < 0)
		sysfatal("cannot create /dev/posix/nanny: %r");
}

static void
exit_on_SA_NOCLDWAIT(char *msg){
	/* we share the father's memory, we can inspect its configuration */
	if(__libposix_signals[PosixSIGCHLD-1].sa_nochildwait){
		/* the parent does not care about us*/
		sys_rfork(RFNOWAIT);
		exits(msg);
	}
}

static void
forward_wait_msg(int father, int child)
{
	int n, mypid;
	PosixSignalInfo si;
	char buf[512], err[ERRMAX], note[512], *fld[5], *tmp, *name;

	mypid = *__libposix_pid;
	name = smprint("signal proxy %d <> %d", father, child);
	snprint(buf, sizeof(buf), "/proc/%d/args", mypid);
	n = sys_open(buf, OWRITE);
	jehanne_write(n, name, strlen(name)+1);
	sys_close(n);

	sys_rfork(RFCNAMEG|RFCENVG|RFNOMNT);

	n = 0;
WaitInterrupted:
	n = sys_await(buf, sizeof buf-1);
	if(n < 0){
		rerrstr(err, ERRMAX);
		if(strstr(err, "no living children") == nil)
			goto WaitInterrupted;
		snprint(note, sizeof(note), "%s: %r", name);
		exit_on_SA_NOCLDWAIT(note);
		postnote(PNPROC, father, note);
		__libposix_sighelper_cmd(PHProcessExited, child);
		exits(note);
	}
	buf[n] = '\0';
	if(jehanne_tokenize(buf, fld, nelem(fld)) != nelem(fld)){
		snprint(note, sizeof(note), "%s: can not parse wait msg: %s", name, buf);
		exit_on_SA_NOCLDWAIT(note);
		postnote(PNPROC, father, note);
		exits(note);
	}
	memset(&si, 0, sizeof(PosixSignalInfo));
	si.si_pid = mypid;
	si.si_signo = PosixSIGCHLD;
	si.si_code = PosixSIChildExited;
	si.si_uid = POSIX_getuid(&n);
	tmp = fld[4];
	if(tmp == nil || tmp[0] == '\0')
		n = 0;
	else if(strcmp(__POSIX_EXIT_PREFIX, tmp) == 0)
		n = atoi(tmp + (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1));
	else if(strcmp(__POSIX_EXIT_SIGNAL_PREFIX, tmp) == 0){
		n = atoi(tmp + (sizeof(__POSIX_EXIT_SIGNAL_PREFIX)/sizeof(char) - 1));
		if(n == PosixSIGTRAP)
			si.si_code = PosixSIChildTrapped;
		else
			si.si_code = PosixSIChildKilled;
	} else
		n = 127;
	si.si_status = n;

	exit_on_SA_NOCLDWAIT("SIGCHLD explicity ignored by parent");

	__libposix_notify_signal_to_process(father, &si);

	__libposix_sighelper_cmd(PHProcessExited, child);
	if(n == 0)
		exits(nil);
	exits(fld[4]);
}


static int
fork_with_sigchld(int *errnop)
{
	int proxy, father, child = -1;
	ChildList *c;
	uint64_t rend;
	char *buf;

	/* Father here:
	 * - create proxy
	 * - wait for child to be ready
	 * - register proxy in children list
	 * - return proxy pid
	 */
	father = getpid();

	switch(proxy = sys_rfork(RFPROC|RFMEM|RFFDG)){
	case -1:
		return -1;
	case 0:
		/* proxy here:
		 * - create child
		 * - start waiting
		 */
		*__libposix_pid = getpid();
		open_sighelper_nanny();
		switch(child = sys_rfork(RFPROC|RFFDG)){
		case -1:
			rend = *__libposix_pid;
			while(sys_rendezvous((void*)rend, "e") == (void*)-1)
				sleep(100);
			sys_rfork(RFNOWAIT);
			exits("rfork (child)");
		case 0:
			/* Beloved child here
			 */
			__libposix_setup_new_process();
			rend = *__libposix_pid;
			while(sys_rendezvous((void*)rend, "d") == (void*)-1)
				sleep(100);
			sys_rfork(RFREND);
			return 0;
		default:
			rend = child;
			while((buf = sys_rendezvous((void*)rend, "")) == (void*)-1)
				sleep(100);
			rend = *__libposix_pid;
			while(sys_rendezvous((void*)rend, "d") == (void*)-1)
				sleep(100);

			/* we share the memory of the parent but we do
			 * not need these that fortunately are on the private stack
			 */
			*__libposix_wait_list = nil;
			*__libposix_child_list = nil;
			forward_wait_msg(father, child);
		}
	default:
		rend = proxy;
		while((buf = sys_rendezvous((void*)rend, "")) == (void*)-1)
			sleep(100);
		if(buf[0] == 'e')
			return -1;
		break;
	}

	/* no need to lock: the child list is private */
	c = malloc(sizeof(ChildList));
	c->pid = proxy;
	c->next = *__libposix_child_list;
	*__libposix_child_list = c;

	return proxy;
}

int
libposix_emulate_SIGCHLD(void)
{
	extern int (*__libposix_fork)(int *errnop);
	__libposix_fork = fork_with_sigchld;
	return 1;
}
