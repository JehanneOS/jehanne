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

int verbose = 1;

void
main(void)
{
	int ret = 0; // success
	uint64_t start, end;
	char *msg;

	start = sys_remove("#c/time");
	if (start == -1){
		print("FAIL: start: remove #c/time: %r");
		exits("FAIL");
	}
	sleep(1);
	end = sys_remove("#c/time");
	if (end == -1){
		print("end: start: remove #c/time: %r");
		exits("FAIL");
	}

	if (end <= start)
		ret = 1;

	if (verbose)
		print("nsec: start %llx, end %llx\n", start, end);
	if(ret){
		msg = smprint("nsec: FAIL: start %llx end %llx",
			start, end);
		print("%s\n", msg);
		exits(msg);
	}

	int fd;
	fd = sys_open("#|", ORDWR);
	sys_mount(fd, -1, "/tmp", MREPL, "", 'M');

	print("PASS\n");
	exits("PASS");
}
