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
#include <libc.h>

int
jehanne_stat(const char* name, uint8_t* edir, int nedir)
{
	int fd, statsz;

	fd = sys_open(name, OSTAT);
	if(fd < 0){
		jehanne_werrstr("stat: %r");
		return fd;
	}
	statsz = sys_fstat(fd, edir, nedir);
	sys_close(fd);
	return statsz;
}

int
jehanne_wstat(const char* name, uint8_t* edir, int nedir)
{
	int fd, statsz;

	fd = sys_open(name, OSTAT);
	if(fd < 0){
		jehanne_werrstr("wstat: %r");
		return fd;
	}
	statsz = sys_fwstat(fd, edir, nedir);
	sys_close(fd);
	return statsz;
}
