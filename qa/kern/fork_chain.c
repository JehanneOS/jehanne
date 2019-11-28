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

#define CHILD_READY(pid) ((long)sys_rendezvous(&r1, (void*)(~(pid))))
//#define C2P_READY(pid) ((long)sys_rendezvous(&r2, (void*)(~(pid))))

int *__target_pid;

static void
forwarding_note_handler(void *ureg, char *note)
{
	postnote(PNPROC, *__target_pid, note);
	sys_noted(NDFLT);
}

static void
donothing(void)
{
	int i = 0;
	while(++i < 100)
		sleep(3000);
	exits(nil);
}

static int
crazy_fork(void)
{
	int father = getpid();
	int p2c;
	long c2p = -1, child = -1;

	switch(p2c = sys_rfork(RFPROC|RFMEM)){
	case -1:
		return -1;
	case 0:
		switch(c2p = sys_rfork(RFPROC|RFMEM)){
		case -1:
			exits("sys_rfork (c2p)");
		case 0:
			switch(child = fork()){
			case -1:
				exits("sys_rfork (child)");
			case 0:
				return 0;
			default:
				while(CHILD_READY(child) == -1)
					;
				*__target_pid = father;
				sys_notify(forwarding_note_handler);
				donothing();
			}
		default:
			while((child = CHILD_READY(-3)) == -1)
				;
			child = ~child;
			*__target_pid = child;
			sys_notify(forwarding_note_handler);
			donothing();
		}
	default:
		break;
	}

	return p2c;
}

int
pass_on_die(void *v, char *s)
{
	if(strncmp(s, "die", 4) == 0){
		print("PASS\n");
		exits("PASS");
	}
	return 0;
}

void
main(void)
{
	int c, target_pid = 0;
	__target_pid = &target_pid;
	
	if (!atnotify(pass_on_die, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	c = crazy_fork();
	switch(c){
	case -1:
		print("FAIL: fork\n");
		exits("FAIL");
	case 0:
		print("child is %d; child's parent is %d\n", getpid(), getppid());
		*__target_pid = getppid();
		sys_notify(forwarding_note_handler);
		/* wait to be killed */
		donothing();
		break;
	default:
		print("father is %d; forked child is %d\n", getpid(), c);
		sleep(1000); /* give children time to sys_notify() */
		print("starting note chain\n");
		postnote(PNPROC, c, "die");
		/* wait to be killed by the chain */
		sleep(30000);
	}
	print("FAIL\n");
	exits("FAIL");
}
