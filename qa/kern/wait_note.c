/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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

int
ignore_note(void *v, char *s)
{
	print("%d: note received: %s\n", getpid(), s);
	return 1;
}

void
main(void)
{
	Waitmsg *w;
	int i = 0, cpid;

	if (!atnotify(ignore_note, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}
	switch(cpid = fork()){
	case -1:
		print("FAIL: fork\n");
		exits("FAIL");
	case 0:
		print("%d: child: waiting 3 sec\n", getpid());
		sleep(3000);
		print("%d: child: sending note to parent\n", getpid());
		postnote(PNPROC, getppid(), "test");
		exits(0);
	default:
		break;
	}
	print("%d: start waiting for child to complete\n", getpid());
InterruptedByNote:
	w = wait();
	if(w == nil){
		if(i==0){
			++i;
			print("%d: wait interrupted by note, try again once\n", getpid());
			goto InterruptedByNote;
		}
		print("FAIL: no wait msg\n");
		exits("FAIL");
	}

	if(cpid != w->pid){
		print("FAIL: wrong Waitmsg: pid = %d instead of expected %d\n", w->pid, cpid);
		exits("FAIL");
	}
	print("Got Waitmsg for child %d\n", w->pid);

	print("PASS\n");
	exits("PASS");
}
