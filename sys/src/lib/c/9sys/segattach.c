/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2017 Giacomo Tesio <giacomo@tesio.it>
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

static int
opensegment(int pid)
{
	char file[128];
	int32_t f;

	sprint(file, "#p/%d/segment", pid);
	f = open(file, OWRITE);
	if(f < 0){
		sprint(file, "/proc/%d/segment", pid);
		f = open(file, OWRITE);
	}
	return f;
}

void*
segattach(int attr, const char *class, void *va, unsigned long len)
{
	int fd;
	long tmp;
	char msg[256];

	tmp = getpid();
	fd = opensegment(tmp);
	if(fd < 0)
		return (void*)-1;

	tmp = snprint(msg, sizeof(msg), "attach 0x%ux %#p %ulld %s", attr, va, len, class);
	tmp = write(fd, msg, tmp);
	close(fd);
	return (void*)tmp;
}

int
segdetach(void *addr)
{
	int fd, tmp;
	char msg[256];

	tmp = getpid();
	fd = opensegment(tmp);
	if(fd < 0)
		return -1;

	tmp = snprint(msg, sizeof(msg), "detach %#p", addr);
	tmp = write(fd, msg, tmp);
	close(fd);
	return tmp;
}

int
segfree(void *addr, unsigned long len)
{
	int fd, tmp;
	char msg[256];

	tmp = getpid();
	fd = opensegment(tmp);
	if(fd < 0)
		return -1;

	tmp = snprint(msg, sizeof(msg), "free %#p %ulld", addr, len);
	tmp = write(fd, msg, tmp);
	close(fd);
	return tmp;
}
