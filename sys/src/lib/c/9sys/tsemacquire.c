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
#include <libc.h>

int
jehanne_tsemacquire(int* addr, long ms)
{
	long wkup;
	wkup = sys_awake(ms);
	while(sys_semacquire(addr, 1) < 0){
		if(jehanne_awakened(wkup)){
			/* copy canlock semantic for return values */
			return 0;
		}
		/* interrupted; try again */
	}
	jehanne_forgivewkp(wkup);
	return 1;
}
