/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016-2017 Giacomo Tesio <giacomo@tesio.it>
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
jehanne_putenv(const char *name, const char *value)
{
	int f;
	int32_t l;
	char path[127+5+1];

	assert(name != nil);
	assert(value != nil);
	if(name[0]=='\0')
		goto BadName;
	if(jehanne_strcmp(name, ".")==0 || jehanne_strcmp(name, "..")==0)
		goto BadName;
	if(jehanne_strchr(name, '/')!=nil)
		goto BadName;

	jehanne_snprint(path, sizeof path, "/env/%s", name);
	if(jehanne_strcmp(path+5, name) != 0)
		goto BadName;

	f = jehanne_ocreate(path, OWRITE, 0664);
	if(f < 0){
		/* try with #e, in case of a previous rfork(RFCNAMEG)
		 */
		jehanne_snprint(path, sizeof path, "#e/%s", name);
		f = jehanne_ocreate(path, OWRITE, 0664);
		if(f < 0)
			return -1;
	}
	l = jehanne_strlen(value);
	if(l > 0 && write(f, value, l) != l){
		close(f);
		return -1;
	}
	close(f);
	return 0;

BadName:
	jehanne_werrstr("bad env name: '%s'", name);
	return -1;
}
