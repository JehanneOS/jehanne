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
	long b, b1;

	b = create("#0/brk/set", -1, 16*1024*1024);
	if(b >= 0){
		print("FAIL: create returned fd %d.\n", b);
		exits("FAIL");
	}
	if(b == -1){
		print("FAIL: create: %r.\n");
		exits("FAIL");
	}
	b1 = brk((void*)b + 16*1024*1024);
	if(b1 == -1){
		print("FAIL: brk: %r.\n");
		exits("FAIL");
	}
	if(b >= b1){
		print("FAIL: b >= b1.\n");
		exits("FAIL");
	}
	print("PASS\n");
	exits("PASS");
}
