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
main(int argc, char**argv)
{
	int fd, n;
	char *path;
	
	path = smprint("/proc/%d/ctl", getpid());
	fd = sys_open(path, OWRITE);
	if(fd < 0){
		print("FAIL: open");
		exits("FAIL");
	}
	n = jehanne_write(fd, "stop", 4);
	if(n < 0){
		print("FAIL: write");
		exits("FAIL");
	}
	sys_close(fd);
	print("PASS\n");
	exits("PASS");
}
