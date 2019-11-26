/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

char *fparts[] =
{
	"add bootldr	0x0000000 0x0040000",
	"add params	0x0040000 0x0080000",
	"add kernel	0x0080000 0x0140000",
	"add user	0x0140000 0x0200000",
	"add ramdisk	0x0200000 0x0600000",
};

void
configpaq(Method* m)
{
	int fd;
	int i;

	if(sys_bind("#F", "/dev", MAFTER) < 0)
		fatal("bind #c");
	if(sys_bind("#p", "/proc", MREPL) < 0)
		fatal("bind #p");
	fd = sys_open("/dev/flash/flashctl", OWRITE);
	if(fd < 0)
		fatal("opening flashctl");
	for(i = 0; i < nelem(fparts); i++)
		if(jehanne_fprint(fd, fparts[i]) < 0)
			fatal(fparts[i]);
	sys_close(fd);
}

int
connectpaq(void)
{
	int  p[2];
	char **arg, **argp;

	jehanne_print("paq...");
	if(jehanne_pipe(p)<0)
		fatal("pipe");
	switch(jehanne_fork()){
	case -1:
		fatal("fork");
	case 0:
		arg = jehanne_malloc(10*sizeof(char*));
		argp = arg;
		*argp++ = "paqfs";
		*argp++ = "-v";
		*argp++ = "-i";
		*argp++ = "/dev/flash/ramdisk";
		*argp = 0;

		if(jehanne_dup(p[0], 0) != 0)
			fatal("jehanne_dup(p[0], 0)");
		if(jehanne_dup(p[1], 1) != 1)
			fatal("jehanne_dup(p[1], 1)");
		sys_close(p[0]);
		sys_close(p[1]);
		sys_exec("/boot/paqfs", (const char**)arg);
		fatal("can't exec paqfs");
	default:
		break;
	}
	jehanne_waitpid();

	sys_close(p[1]);
	return p[0];
}
