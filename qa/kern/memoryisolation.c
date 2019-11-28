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

#define MSGTPL "Hello from %d!"

int verbose = 1;

int
passOnReadFault(void *v, char *s)
{
	if(strncmp(s, "sys: trap: fault read ", 4) == 0){
		if(verbose)
			print("%d: noted: %s\n", getpid(), s);
		print("PASS\n");
		exits("PASS");
	}
	return 0;
}

void
main(void)
{
	int p[2], pid, i;
	char *msg, buf[128];
	
	if (!atnotify(passOnReadFault, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	pipe(p);
	memset(buf, 0, sizeof buf);
	
	switch((pid = sys_rfork(RFPROC))){
		case -1:
			fprint(2, "FAIL: sys_rfork: %r\n");
			exits("FAIL");
			break;
		case 0:
			msg = smprint(MSGTPL, getpid());
			i = sprint(buf, "%p", msg);
			if(verbose){
				fprint(2, "address %p, msg %s\n", msg, msg);
				fprint(2, "sending %s\n", buf);
			}
			if(jehanne_write(p[0], buf, i) < 0){
				fprint(2, "FAIL: write: %r\n");
				exits("FAIL");
			}
			exits("PASS");
			break;
		default:
			buf[0] = '0';
			buf[1] = 'x';
			if((i = jehanne_read(p[1], buf+2, (sizeof buf) - 2)) <= 0){
				fprint(2, "FAIL: read: %r\n");
				exits("FAIL");
			}
			msg = (char*)atoll(buf);
			if(verbose){
				fprint(2, "received %s\n", buf);
				fprint(2, "address %p\n", msg);
			}
			i = sprint(buf, MSGTPL, pid);
			if(strncmp(msg, buf, i) == 0){
				fprint(2, "FAIL: no memory isolation: read '%s' from child address space\n", msg);
				exits("FAIL");
			}
			print("PASS\n");
			exits("PASS");
			break;
	}
}
