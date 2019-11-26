/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include <envvars.h>
#include <auth.h>
#include <9P2000.h>
#include "../boot/boot.h"

char	*authaddr;
static void glenda(void);

void
authentication(int cpuflag)
{
	char *argv[16], **av;
	int ac;

	if(jehanne_access(factotumPath, AEXEC) < 0 || jehanne_getenv(ENV_USER) != nil){
		glenda();
		return;
	}

	/* start agent */
	ac = 0;
	av = argv;
	av[ac++] = "factotum";
	if(jehanne_getenv("debugfactotum"))
		av[ac++] = "-p";
//	av[ac++] = "-d";		/* debug traces */
//	av[ac++] = "-D";		/* 9p messages */
	if(cpuflag)
		av[ac++] = "-S";
	else
		av[ac++] = "-u";
	av[ac++] = "-sfactotum";
	if(authaddr != nil){
		av[ac++] = "-a";
		av[ac++] = authaddr;
	}
	av[ac] = 0;
	switch(jehanne_fork()){
	case -1:
		fatal("starting factotum");
	case 0:
		sys_exec(factotumPath, (const char**)av);
		fatal("execing factotum");
	default:
		break;
	}

	/* wait for agent to really be there */
	while(jehanne_access("/mnt/factotum", AEXIST) < 0)
		jehanne_sleep(250);

	if(cpuflag)
		return;
}

static void
glenda(void)
{
	int fd;
	char *s;

	s = jehanne_getenv(ENV_USER);
	if(s == nil)
		s = "glenda";

	fd = sys_open("#c/hostowner", OWRITE);
	if(fd >= 0){
		if(jehanne_write(fd, s, jehanne_strlen(s)) != jehanne_strlen(s))
			jehanne_fprint(2, "setting #c/hostowner to %s: %r\n", s);
		sys_close(fd);
	}
	jehanne_fprint(2, "Set hostowner to %s\n", s);
}
