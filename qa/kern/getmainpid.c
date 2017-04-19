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
#include <lib9.h>

void
main(void)
{
	int32_t pid, mainpid;
	Waitmsg *w;

	switch(fork()){
		case 0:
			mainpid = getmainpid();
			pid = getppid();
			if(mainpid != pid){
				print("FAIL: getmainpid() = %d, getppid() = %d\n", mainpid, pid);
				exits("FAIL");
			}
			exits(nil);
		break;
		default:
			mainpid = getmainpid();
			pid = getpid();
			if(mainpid != pid){
				print("FAIL: getmainpid() = %d, getpid() = %d\n", mainpid, pid);
				exits("FAIL");
			}
			w = wait();
			if(w->msg[0])
				exits("FAIL");
			break;
	}
	
	print("PASS\n");
	exits("PASS");
}
