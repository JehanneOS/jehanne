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

int sync[2];

void
setup_ns(void)
{
	int fd;
	if(bind("#|", "/", MREPL) < 0){
		print("FAIL: rfork\n");
		write(sync[0], "FAIL", 5);
		return;
	}
	fd = open("/data", OWRITE);
	if(fd < 0){
		print("FAIL: open(/data)\n");
		write(sync[0], "FAIL", 5);
		return;
	}
	write(fd, "hi!", 4);
	close(fd);
	write(sync[0], "DONE", 5);
	sleep(10);
}

void
main(void)
{
	int child, fd;
	char buf[64];

	rfork(RFNOTEG|RFNAMEG);

	pipe(sync);
	switch(child = rfork(RFPROC|RFCNAMEG|RFNOWAIT)){
	case -1:
		print("FAIL: rfork\n");
		exits("FAIL");
	case 0:
		setup_ns();
		exits(nil);
	default:
		break;
	}

	read(sync[1], buf, sizeof(buf));
	if(strcmp("DONE", buf) != 0)
		exits("FAIL");

	snprint(buf, sizeof(buf), "/proc/%d/ns", child);
	fd = open(buf, OWRITE);
	if(fd < 0){
		print("FAIL: open(%s)\n", buf);
		exits("FAIL");
	}
	write(fd, "clone", 6);
	close(fd);

	memset(buf, 0, sizeof(buf));
	fd = open("/data1", OREAD);
	if(fd < 0){
		print("FAIL: open(/data1)\n");
		exits("FAIL");
	}
	read(fd, buf, sizeof(buf));
	close(fd);

	if(strcmp("hi!", buf) == 0){
		print("PASS: read '%s' from /data1\n", buf);
		exits("PASS");
	}
	print("FAIL: unexpected string from %d: %s\n", child, buf);
	exits("FAIL");
}
