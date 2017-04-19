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
/*
 * Print working (current) directory
 */

void
main(int argc, char *argv[])
{
	long len;
	char path[512];
	char *ppath = path;

	len = getwd(ppath, sizeof(path));
	if(len == 0)
		goto Error;
	if(len < 0){
		len = ~len;
		ppath = malloc(len*sizeof(char));
		if(getwd(ppath, len*sizeof(char)) <= 0)
			goto Error;
	}
	print("%s\n", ppath);
	exits(0);

Error:
	fprint(2, "pwd: %r\n");
	exits("getwd");
}
