/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

/*
 * HACK - take over from boot since file system is not
 * available on a pipe
 */

void
configsac(Method *mp)
{
	int fd;
	char cmd[64];

	USED(mp);

	/*
	 *  create the name space, mount the root fs
	 */
	if(sys_bind("/", "/", MREPL) < 0)
		fatal("bind /");
	if(sys_bind("#C", "/", MAFTER) < 0)
		fatal("bind /");

	/* fixed sysname - enables correct namespace file */
	fd = sys_open("#c/sysname", OWRITE);
	if(fd < 0)
		fatal("open sysname");
	jehanne_write(fd, "brick", 5);
	sys_close(fd);

	fd = sys_open("#c/hostowner", OWRITE);
	if(fd < 0)
		fatal("open sysname");
	jehanne_write(fd, "brick", 5);
	sys_close(fd);

	jehanne_sprint(cmd, "/arch/%s/init", cputype);
	jehanne_print("starting %s\n", cmd);
	jehanne_execl(cmd, "init", "-c", 0);
	fatal(cmd);
}

int
connectsac(void)
{
	/* does not get here */
	return -1;
}
