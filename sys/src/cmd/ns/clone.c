/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>
#include <envvars.h>

void
main(int argc, char *argv[])
{
	int fd;
	char buf[256], *cmd, **args;

	sys_rfork(RFNAMEG);
	if(argc < 3){
		fprint(2, "usage: ns/clone pid cmd [args...]\n");
		exits("usage");
	}

	snprint(buf, sizeof(buf), "/proc/%s/ns", argv[1]);
	fd = sys_open(buf, OWRITE);
	if(fd < 0){
		print("cannot open %s\n", buf);
		exits("invalid target pid");
	}
	jehanne_write(fd, "clone", 6);
	sys_close(fd);

	cmd = strdup(argv[2]);
	args = &argv[2];
	if(cmd[0] == '/' 
	||(cmd[0] == '.' && (cmd[1] == '/' || (cmd[1] == '.' && cmd[2] == '/')))){
		sys_exec(cmd, (const char**)args);
		sysfatal("exec %s failed: %r", cmd);
	}

	jehanne_pexec(cmd, args);
	sysfatal("exec %s failed: %r", cmd);
}
