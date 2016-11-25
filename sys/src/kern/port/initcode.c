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

char boot[] = "/boot/boot";
char bootdir[] = "/boot";
char bootname[] = "boot";
char rofs[] = "/boot/rofs";
char rofsn[] = "rofs";
char initrd[] = "/boot/initrd";
char bootfs[] = "#s/bootfs";
char dev[] = "/dev";
char c[] = "#c";
char e[] = "#e";
char ec[] = "#ec";
char s[] = "#s";
char srv[] = "/srv";
char env[] = "/env";
char arg1[] = "-qas";
char arg2[] = "-c";
char arg3[] = "8";
char arg4[] = "-S";
char arg5[] = "bootfs";

static int
initboot(void)
{
	int pid, i;
	char * const args[8] = {
		rofs,
		arg1,
		arg2, arg3,
		arg4, arg5,
		initrd,
		0
	};

	if (access(boot, AEXEC) == 0)
		return 0;
	if (access(rofs, AEXEC) < 0)
		return -1;
	if (access(initrd, AREAD) < 0)
		return -1;

	switch(pid = rfork(RFFDG|RFREND|RFPROC)){
	case -1:
		return -1;
	case 0:
		exec(rofs, (const char**)args);
		exits("initcode: exec: rofs");
	default:
		/* wait for agent to really be there */
		i = 0;
		while(access(bootfs, AREAD) < 0){
			sleep(250);
			i += 250;
			if(i > 30000)	// 30 seconds are enough
				return -1;
		}
		break;
	}
	if((i = open(bootfs, ORDWR)) < 0)
		return -1;
	if(mount(i, -1, bootdir, MREPL, "", 'M') < 0)
		return -1;
	remove(bootfs);
	return access(boot, AEXEC);
}

void
startboot(char *argv0, char **argv)
{
	int i;
	char buf[200];
	char * const args[2] = {
		bootname,
		nil
	};
	for(i = 0; i < sizeof buf; ++i)
		buf[i] = '\0';

	bind(c, dev, MAFTER);
	bind(ec, env, MAFTER);
	bind(e, env, MCREATE|MAFTER);
	bind(s, srv, MREPL|MCREATE);

	if (initboot() == 0)
		exec(boot, (const char**)args);

	errstr(buf, sizeof buf - 1);
	_exits(buf);
}
