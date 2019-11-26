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

extern void	**_privates;
extern int	_nprivates;

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
char self[] = "#0";
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

	if (jehanne_access(boot, AEXEC) == 0)
		return 0;
	if (jehanne_access(rofs, AEXEC) < 0)
		return -1;
	if (jehanne_access(initrd, AREAD) < 0)
		return -1;

	switch(pid = sys_rfork(RFFDG|RFREND|RFPROC)){
	case -1:
		return -1;
	case 0:
		sys_exec(rofs, (const char**)args);
		jehanne_exits("initcode: exec: rofs");
	default:
		/* wait for agent to really be there */
		i = 0;
		while(jehanne_access(bootfs, AREAD) < 0){
			jehanne_sleep(250);
			i += 250;
			if(i > 30000)	// 30 seconds are enough
				return -1;
		}
		break;
	}
	if((i = sys_open(bootfs, ORDWR)) < 0)
		return -1;
	if(sys_mount(i, -1, bootdir, MREPL, "", '9') < 0)
		return -1;
	sys_remove(bootfs);
	return jehanne_access(boot, AEXEC);
}

void
startboot(int argc, char **argv)
{
	/* Initialize per process structures on the stack */
	int i;
	char buf[200];
	char * const args[2] = {
		bootname,
		nil
	};

	for(i = 0; i < sizeof buf; ++i)
		buf[i] = '\0';

	sys_bind(self, dev, MREPL);
	sys_bind(c, dev, MAFTER);
	sys_bind(ec, env, MAFTER);
	sys_bind(e, env, MCREATE|MAFTER);
	sys_bind(s, srv, MREPL|MCREATE);

	if (initboot() == 0)
		sys_exec(boot, (const char**)args);

	sys_errstr(buf, sizeof buf - 1);
	sys__exits(buf);
}
