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

char*
jehanne_getenv(const char *name)
{
	int f;
	int32_t l;
	char path[127+5+1], *value;

	assert(name != nil);
	if(name[0]=='\0')
		goto BadName;
	if(jehanne_strcmp(name, ".")==0 || jehanne_strcmp(name, "..")==0)
		goto BadName;
	if(jehanne_strchr(name, '/')!=nil)
		goto BadName;

	jehanne_snprint(path, sizeof path, "/env/%s", name);
	if(jehanne_strcmp(path+5, name) != 0)
		goto BadName;

	f = sys_open(path, OREAD);
	if(f < 0){
		/* try with #e, in case of a previous sys_rfork(RFCNAMEG)
		 *
		 * NOTE: /env is bound to #ec by default, so we
		 * cannot simply use always #e instead of /env. Also
		 * using #ec when the open in #e fails is both
		 * slow and not flexible enough.
		 */
		jehanne_snprint(path, sizeof path, "#e/%s", name);
		f = sys_open(path, OREAD);
		if(f < 0)
			return nil;
	}
	l = sys_seek(f, 0, 2);
	value = jehanne_malloc(l+1);
	if(value == nil)
		goto Done;
	jehanne_setmalloctag(value, jehanne_getcallerpc());
	sys_seek(f, 0, 0);
	if(jehanne_read(f, value, l) >= 0)
		value[l] = '\0';
Done:
	sys_close(f);
	return value;

BadName:
	jehanne_werrstr("bad env name: '%s'", name);
	return nil;
}
