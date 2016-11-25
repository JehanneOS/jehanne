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

void*
bsearch(const void* key, const void* base, long nmemb, int size,
		int (*compar)(const void*, const void*))
{
	long i, bot, top, new;
	void *p;

	if(nmemb == 0 || size == 0)
		return nil;
	bot = 0;
	top = bot + nmemb - 1;
	while(bot <= top){
		new = (top + bot)/2;
		p = (char *)base+new*size;
		i = (*compar)(key, p);
		if(i == 0)
			return p;
		if(i > 0)
			bot = new + 1;
		else
			top = new - 1;
	}
	return nil;
}
