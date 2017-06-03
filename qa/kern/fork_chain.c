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

int r1;
int r2;

#define CHILD_READY(pid) ((long)rendezvous(&r1, (void*)(~(pid))))
#define C2P_READY(pid) ((long)rendezvous(&r2, (void*)(~(pid))))

int *__libposix_sigchld_target_pid;

static void
release_inherited_resources(void)
{
	notify(nil);
	rfork(RFCNAMEG|RFCENVG|RFNOTEG|RFCFDG);
	bind("#p", "/proc", MREPL);
	rfork(RFNOMNT);
}

static void
forwarding_note_handler(void *ureg, char *note)
{
	postnote(PNPROC, *__libposix_sigchld_target_pid, note);
	noted(NCONT);
}

static void
forward_wait_msg(int sigchld_receiver, char *name)
{
	int n;
	char buf[512], err[ERRMAX], note[512], *fld[5];

	snprint(buf, sizeof(buf), "/proc/%d/args", getpid());
	n = open(buf, OWRITE);
	write(n, name, strlen(name)+1);
	close(n);

	n = 0;
WaitInterrupted:
	n = await(buf, sizeof buf-1);
	if(n < 0){
		rerrstr(err, ERRMAX);
		if(strstr(err, "no living children") == nil)
			goto WaitInterrupted;
		snprint(note, sizeof(note), "%s: %r", name);
		if(sigchld_receiver)
			postnote(PNPROC, sigchld_receiver, note);
		exits(note);
	}
	buf[n] = '\0';
	if(jehanne_tokenize(buf, fld, nelem(fld)) != nelem(fld)){
		snprint(note, sizeof(note), "%s: couldn't parse wait message", name);
		if(sigchld_receiver)
			postnote(PNPROC, sigchld_receiver, note);
		exits(note);
	}
	snprint(note, sizeof(note), "posix: 20");
	if(sigchld_receiver){
		postnote(PNPROC, sigchld_receiver, note);
	}
	exits(fld[4]);
}


static int
fork_with_sigchld(void)
{
	int father = getpid();
	int p2c;
	long c2p = -1, child = -1;
	char proxy_name[256];

	/* Father here:
	 * - create P2C
	 * - wait for C2P to be ready
	 * - register P2C in children list
	 * - return P2C pid
	 */
	switch(p2c = rfork(RFPROC|RFMEM)){
	case -1:
		return -1;
	case 0:
		/* P2C here:
		 * - create C2P
		 * - wait for the child pid
		 * - release all inherited resources
		 * - install forwarding_note_handler
		 * - send to father the C2P pid
		 * - start waiting for the child
		 */
		switch(c2p = rfork(RFPROC|RFMEM)){
		case -1:
			while(C2P_READY(-2) == -1)
				;
			exits("rfork (c2p)");
		case 0:
			/* C2P here:
			 * - create child
			 * - wait for it to get a copy of everything
			 * - release all inherited resources
			 * - install forwarding_note_handler
			 * - send to P2C the child pid
			 * - start forwarding notes to the father
			 */
			switch(child = fork()){
			case -1:
				while(CHILD_READY(-2) == -1)
					;
				exits("rfork (child)");
			case 0:
				/* Beloved child here
				 */
//				__libposix_setup_new_process();
				return 0;
			default:
				release_inherited_resources();
				*__libposix_sigchld_target_pid = father;
				notify(forwarding_note_handler);
				snprint(proxy_name, sizeof(proxy_name), "libposix signal proxy %d < %d", father, child);
				while(CHILD_READY(child) == -1)
					;
				forward_wait_msg(father, proxy_name);
			}
		default:
			while((child = CHILD_READY(-3)) == -1)
				;
			child = ~child;
			if(child < 0){
				while(C2P_READY(-2) == -1)
					;
				waitpid();
				exits("rfork (child)");
			}
			release_inherited_resources();
			*__libposix_sigchld_target_pid = child;
			notify(forwarding_note_handler);
			snprint(proxy_name, sizeof(proxy_name), "libposix signal proxy %d > %d", father, child);
			while(C2P_READY(c2p) == -1)
				;
			forward_wait_msg(0, proxy_name);
		}
	default:
		while((c2p = C2P_READY(-3)) == -1)
			;
		c2p = ~c2p;
		if(c2p < 0){
			waitpid();
			return -1;
		}
		break;
	}

	print("f %d; p2c %d, c2p %d\n", father, p2c, c2p);

	return p2c;
}

int
passOnSIGCHLD(void *v, char *s)
{
	if(strncmp(s, "posix: 20", 4) == 0){
		print("PASS\n");
		exits("PASS");
	}
	return 0;
}

void
main(void)
{
	int c, target_pid = 0;
	__libposix_sigchld_target_pid = &target_pid;
	
	if (!atnotify(passOnSIGCHLD, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	c = fork_with_sigchld();
	switch(c){
	case -1:
		print("FAIL: fork\n");
		exits("FAIL");
	case 0:
		/* wait to be killed */
		print("child is %d\n", getpid());
//		for(;;);
		sleep(30000);
		break;
	default:
		print("father is %d\n", getpid());
		postnote(PNPROC, c, "die");
		/* wait to be killed */
		sleep(30000);
	}
	print("FAIL\n");
	exits("FAIL");
}
