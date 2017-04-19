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

#define GiB 1024*1024*1024

void
main(void)
{
	char *p, *q, *e;
	
	p = malloc(GiB);
	
	if(p == nil){
		fprint(2, "FAIL: malloc(GiB)\n");
		exits("FAIL");
	}
	e = p + GiB;
	
	for(q = p; 
		q < e; 
		q += 512){
		if((q - p) % (32*1024*1024) == 0)
			fprint(2, "setting %#p (off %d, %d MiB)\n", q, q - p, (q - p) / (1024*1024));
		*q = 1;
	}
	for(q = p; 
		q < e; 
		q += 512){
		if(*q != 1){
			fprint(2, "FAIL: memory check\n");
			exits("FAIL");
		}
	}

	print("PASS\n");
	exits("PASS");
}
