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
jehanne_access(const char *name, int mode)
{
	int tmp, reqmode;
	Dir *db;
	char *user;

	static char omode[] = {
		OSTAT,
		OEXEC,
		OWRITE,
		ORDWR,
		OREAD,
		OEXEC,  /* 5=4+1 READ|EXEC, EXEC is enough */
		ORDWR,
		ORDWR   /* 7=4+2+1 READ|WRITE|EXEC, ignore EXEC */
	};

	reqmode = omode[mode&AMASK];
	tmp = open(name, reqmode);
	if(tmp >= 0){
		close(tmp);
		return 0;
	}
	db = jehanne_dirstat(name);
	if(db != nil){
		if(db->mode & DMDIR){
			/* check other first */
			tmp = db->mode & reqmode;
			if(tmp != reqmode){
				/* TODO: make something better */
				user = jehanne_getuser();
				if(jehanne_strcmp(user, db->gid) == 0){
					/* check group */
					tmp = db->mode & (reqmode << 3);
					tmp = tmp >> 3;
					if(tmp != reqmode && jehanne_strcmp(user, db->uid)== 0){
						/* check user */
						tmp = db->mode & (reqmode << 6);
						tmp = tmp >> 6;
					}
				} else if (jehanne_strcmp(user, db->uid)== 0){
					/* check user */
					tmp = db->mode & (reqmode << 6);
					tmp = tmp >> 6;
				}
			}
		}
		jehanne_free(db);
		if(tmp == reqmode)
			return 0;
	}
	return -1;
}
