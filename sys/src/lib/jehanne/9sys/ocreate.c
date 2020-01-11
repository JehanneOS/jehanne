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

/* ocreate works like the Plan 9 create(2) syscall, but with different races.
 * In Plan 9 there is a race due to the different behaviour between the
 * create syscall and the Tcreate message in 9P2000 when the file already exists:
 * see https://github.com/brho/plan9/blob/master/sys/src/9/port/chan.c#L1564-L1603
 * for details.
 *
 * In Jehanne the create syscall fails on existing files just like the Tcreate message.
 * However the Plan 9/UNIX semantic is often useful, thus ocreate mimic it in userspace.
 * As wisely noted by Charles Forsyth, such implementation introduce a different race
 * due to the multiple evaluations of the path: on concurrent namespace changes, the
 * different syscalls here could be handled by different devices/fileservers.
 * However, given the user is responsible of such namespace changes, we prefer this race
 * to the original Plan 9 one.
 *
 * For more info see http://marc.info/?t=146412533100003&r=1&w=2
 */
int
jehanne_ocreate(const char *path, unsigned int omode, unsigned int perm)
{
	int fd;
	Dir *s;

	fd = sys_open(path, omode|OTRUNC);
	if(fd < 0){
		fd = sys_create(path, omode, perm);
		if(fd < 0){
			fd = sys_open(path, omode|OTRUNC);
			if(fd < 0)
				goto Done;
		} else {
			goto Done;
		}
	}

	s = jehanne_dirfstat(fd);
	if(s == nil){
		sys_close(fd);
		return -1;
	}
	if(s->mode != perm){
		s->mode = perm;
		jehanne_dirfwstat(fd, s); /* we ignore the return value, the device/server is allowed to ignore us */
	}
	jehanne_free(s);

Done:
	return fd;
}
