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
#include <lib9.h>

/* check devdup Nctl format */


void
main(void)
{
	char buf[256];
	int fd, r;

	fd = sys_open("/fd/0ctl", OREAD|OCEXEC);
	if(fd < 0)
		goto Fail;
	r = jehanne_read(fd, buf, sizeof(buf));
	if(r < 0)
		goto Fail;
	sys_close(fd);
	print("Got 0ctl: %s\n", buf);

	fd = sys_open("/fd/1ctl", OREAD|OCEXEC);
	if(fd < 0)
		goto Fail;
	r = jehanne_read(fd, buf, sizeof(buf));
	if(r < 0)
		goto Fail;
	sys_close(fd);
	print("Got 1ctl: %s\n", buf);

	fd = sys_open("/fd/2ctl", OREAD|OCEXEC);
	if(fd < 0)
		goto Fail;
	r = jehanne_read(fd, buf, sizeof(buf));
	if(r < 0)
		goto Fail;
	print("Got 2ctl: %s\n", buf);

	snprint(buf, sizeof(buf), "/fd/%dctl", fd);
	fd = sys_open(buf, OREAD);
	if(fd < 0)
		goto Fail;
	r = jehanne_read(fd, buf, sizeof(buf));
	if(r < 0)
		goto Fail;
	print("Got: %s\n", buf);
	sys_close(fd);
	exits(nil);

Fail:
	print("FAIL\n");
	exits("FAIL");
}
