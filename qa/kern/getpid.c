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
	int pid;
	char err[ERRMAX];

	pid = getmainpid(); // this comes from exec() syscall

	if(getpid() != pid){
		print("FAIL: getpid() != getmainpid()\n");
		exits("FAIL");
	}
	rfork(RFNOMNT);
	if(getpid() != pid){
		print("FAIL: rfork(RFNOMNT); getpid() != getmainpid()\n");
		exits("FAIL");
	}
	rfork(RFNOMNT|RFCNAMEG);
	if(getpid() != -1){
		print("FAIL: rfork(RFNOMNT|RFCNAMEG)); getpid() != -1\n");
		exits("FAIL");
	}
	rerrstr(err, ERRMAX);
	if(strcmp("getpid: cannot open neither #c/pid nor /dev/pid", err)){
		print("FAIL: rfork(RFNOMNT|RFCNAMEG)); getpid() set errstr '%s'\n", err);
		exits("FAIL");
	}

	print("PASS\n");
	exits("PASS");
}
