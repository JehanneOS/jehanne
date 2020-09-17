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
#include <envvars.h>

int
jehanne_pexec(const char *f, char *args[])
{
	char *epath, *path, *entries[33];
	int n, i;

	if(f[0] == '/' || (epath = jehanne_getenv(ENV_PATH)) == nil)
		return sys_exec(f, (const char**)args);

	n = jehanne_getfields(epath, entries, nelem(entries), 1, ":");
	for(i = 0; i < n; ++i){
		path = jehanne_smprint("%s/%s", entries[i], f);
		sys_exec(path, (const char**)args);
		jehanne_free(path);
	}

	jehanne_free(epath);
	return -1;
}
