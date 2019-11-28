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

int spin;
int ok[2];
void
main(void)
{
	char **test;
	test = (char **)privalloc();
	*test = smprint("Hello from %d (at %p)\n", getpid(), test);
	
	sys_rfork(RFNOTEG);
	switch(sys_rfork(RFPROC|RFMEM)){
		case -1:
			exits("sys_rfork");
		case 0:
			*test = smprint("Hello from %d (at %p)\n", getpid(), test);
			spin = 1;
			while(spin)
				;
			print("on %d: %s", getpid(), *test);
			exits(nil);
			break;
		default:
			while(spin == 0)
				;
	}
	switch(sys_rfork(RFPROC|RFMEM)){
		case -1:
			exits("sys_rfork");
		case 0:
			*test = smprint("Hello from %d (at %p)\n", getpid(), test);
			spin = 2;
			while(spin)
				;
			print("on %d: %s", getpid(), *test);
			exits(nil);
			break;
		default:
			while(spin == 1)
				;
	}
	
	spin = 0;
	sleep(500);
	print("on %d: %s", getpid(), *test);
	exits("PASS");
}
