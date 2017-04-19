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

int verbose = 1;

void
main(void)
{
	void *seg;
	char *p;

	seg = segattach(15, "memory", (void*)0x80000000, 1024*1024);
	if(seg == (void*)-1){
		print("FAIL: segattach: %r\n");
		exits("FAIL");
	}
	if(seg != (void*)0x80000000){
		print("FAIL: segattach: wrong segment base for memory class %#p\n", seg);
		exits("FAIL");
	}
	for(p = seg; p < (char*)seg+(1024*1024); p += 512)
		*p = 1;

	for(p = seg; p < (char*)seg+(1024*1024); p += 512){
		if(*p != 1){
			print("FAIL: segattach: unable to write bytes in attached segment\n");
			exits("FAIL");
		}
	}

	p = seg + 4096*5+10;
	if(segfree(p, 4096*10) < 0){
		print("FAIL: segfree: %r\n");
		exits("FAIL");
	}
	for(p = seg; p < (char*)seg+(1024*1024); p += 512){
		if(*p != 1){
//			print("segfree: found clean address at %#p\n", p);
			break;
		}
	}
	if(p == (char*)seg+(1024*1024)){
		print("FAIL: segfree: no page previously freed had been faulted\n");
		exits("FAIL");
	}

	if(segdetach(seg) < 0){
		print("FAIL: segdetach: %r\n");
		exits("FAIL");
	}

	print("PASS\n");
	exits("PASS");
}
