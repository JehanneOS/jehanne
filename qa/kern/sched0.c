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

/* verify that the scheduler give each process a chance to run
 * with a single cpu (x86) this test do not pass on plan9/9front
 * (last check on 2016-10-03).
 */

int spin;
void
main(void)
{
	sys_rfork(RFNOTEG);
	switch(sys_rfork(RFPROC|RFMEM)){
	case -1:
		exits("sys_rfork");
	case 0:
		spin = 1;
		while(spin)
			;
		exits(nil);
	default:
		while(spin == 0)
			;
	}
	switch(sys_rfork(RFPROC|RFMEM)){
	case -1:
		exits("sys_rfork");
	case 0:
		spin = 2;
		while(spin)
			;
		exits(nil);
	default:
		while(spin == 1)
			;
	}

	spin = 0;
	sleep(500);
	print("PASS\n");
	exits("PASS");
}
