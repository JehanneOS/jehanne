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

void
main(void)
{
	char *err = nil;
	if(access("/tmp", AEXIST) != 0)
		err = "/tmp does not exists";
	else if(access("/tmp", AREAD) != 0)
		err = "/tmp is not readable";
/* NOTE:
 * In Plan 9 access(AWRITE) and access(AEXEC) in directories
 * fail despite the actual permission of the directory.
 *
	else if(access("/tmp", AWRITE) != 0)
		err = "/tmp is not writeable";
	else if(access("/tmp", AEXEC) != 0)
		err = "/tmp is not traversable";
*/
	if(err == nil){
		print("PASS\n");
		exits("PASS");
	} else {
		print("FAIL: %s\n", err);
		exits("FAIL");
	}
}
