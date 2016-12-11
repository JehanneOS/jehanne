/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
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
#include <libc.h>

void
main(void)
{
	int ppid;
	char err[ERRMAX];
	Waitmsg *w;

	if(rfork(RFPROC) == 0){
		ppid = getmainpid(); // this comes from exec() syscall

		if(getppid() != ppid){
			print("FAIL: getppid() != getmainpid()\n");
			exits("FAIL");
		}
		rfork(RFNOMNT);
		if(getppid() != ppid){
			print("FAIL: rfork(RFNOMNT); getppid() != getmainpid()\n");
			exits("FAIL");
		}
		rfork(RFNOMNT|RFCNAMEG);
		if(getppid() != ppid){
			print("FAIL: rfork(RFNOMNT|RFCNAMEG)); getppid() != getmainpid()\n");
			exits("FAIL");
		}
		exits(nil);
	} else {
		w = wait();
		if(w->msg[0]){
			exits("FAIL");
		}
		print("PASS\n");
		exits("PASS");
	}
}
