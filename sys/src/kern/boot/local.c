/*
 * Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

static char *disk;

void
configlocal(Method *mp)
{
	char *path, *cmd;
	int try;
	if(*sys == '/' || *sys == '#'){
		/*
		 *  if the user specifies the disk in the boot cmd or
		 * 'root is from' prompt, use it
		 */
		disk = sys;
	} else if(mp->arg){
		/*
		 *  a default is supplied when the kernel is made
		 */
		disk = mp->arg;
	} else if(*bootdisk){
		/*
		 *  an environment variable from a pc's plan9.ini or
		 *  from the mips nvram or generated by the kernel
		 *  is the last resort.
		 */
		disk = bootdisk;
	} else {
		disk = "#s/sdE0/";
	}
jehanne_print("configlocal: disk is %s\n", disk);
	/* if we've decided on one, pass it on to all programs */
	if(disk) {
		setenv("bootdisk", disk);
		setenv("nvram", jehanne_smprint("%s/nvram", disk));
	}

	if (jehanne_access(disk, AEXIST) < 0){
		jehanne_print("configlocal: cannot access %s\n", disk);
		shell("-i", nil);
	} else {
		try = 0;
		path = jehanne_smprint("%s/plan9", disk);
		cmd = jehanne_smprint("%s -p '%s/data' >> '%s/ctl'", fdiskPath, disk, disk);
		while(jehanne_access(path, AEXIST) < 0){
			if(try++)
				jehanne_print("%s: try %d\n", cmd, try);
			if(try > 4)
				shell("-i", nil);
			shell("-c", cmd);
			jehanne_sleep(250);
		}
		jehanne_free(cmd);
		jehanne_free(path);
		try = 0;
		path = jehanne_smprint("%s/fs", disk);
		cmd = jehanne_smprint("%s -p '%s/plan9' >> '%s/ctl'", prepPath, disk, disk);
		while(jehanne_access(path, AEXIST) < 0){
			if(try++)
				jehanne_print("%s: try %d\n", cmd, try);
			if(try > 4)
				shell("-i", nil);
			shell("-c", cmd);
			jehanne_sleep(250);
		}
		jehanne_free(cmd);
		jehanne_free(path);
	}
}


static int
print1(int fd, char *s)
{
	return jehanne_write(fd, s, jehanne_strlen(s));
}

void
configloopback(void)
{
	int fd;

	if((fd = sys_open("/net/ipifc/clone", ORDWR)) < 0){
		sys_bind("#I", "/net", MAFTER);
		if((fd = sys_open("/net/ipifc/clone", ORDWR)) < 0)
			fatal("open /net/ipifc/clone for loopback");
	}
	if(print1(fd, "bind loopback /dev/null") < 0
	|| print1(fd, "add 127.0.0.1 255.255.255.255") < 0)
		fatal("write /net/ipifc/clone for loopback");
	sys_close(fd);
}

int
connecthjfs(void)
{
	int fd;
	char partition[128];
	char *dev;

	if(jehanne_access(hjfsPath, AEXEC) < 0){
		return -1;
	}

	/* look for hjfs partition */
	dev = disk ? disk : bootdisk;
	jehanne_snprint(partition, sizeof partition, "%s/fs", dev);

	/* start hjfs */
	jehanne_print("hjfs(%s)...", partition);
	shell("-c", jehanne_smprint("%s -f '%s' -n 'boot'", hjfsPath, partition));
	fd = sys_open("#s/boot", ORDWR);
	if(fd < 0){
		jehanne_print("open #s/boot: %r\n");
		return -1;
	}
	return fd;
}

int
connectlocal(void)
{
	int fd;

	if(sys_bind("#0", "/dev", MREPL) < 0)
		fatal("bind #0");
	if(sys_bind("#c", "/dev", MAFTER) < 0)
		fatal("bind #c");
	if(sys_bind("#p", "/proc", MREPL) < 0)
		fatal("bind #p");
	sys_bind("#S", "/dev", MAFTER);
	sys_bind("#k", "/dev", MAFTER);
	sys_bind("#æ", "/dev", MAFTER);
	if((fd = connecthjfs()) < 0){
		shell("-i", nil);
		return -1;
	}
	return fd;
}
