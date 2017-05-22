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

#define Mb (1024*1024)

/* This program cause an exception if libc is compiled with -O2
 */
void
main(int argc, char**argv)
{
	char *v;
	int i, j;

	j = 5;
	v = malloc(j*Mb);
	for(i = 0; i < j*Mb; i += 512)
		v[i] = 'a';
	free(v);
	
	print("PASS\n");
	exits("PASS");
}
