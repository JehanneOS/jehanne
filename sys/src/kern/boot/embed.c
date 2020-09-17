/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

static char *paqfile;

void
configembed(Method *m)
{
	if(*sys == '/' || *sys == '#'){
		/*
		 *  if the user specifies the disk in the boot cmd or
		 * 'root is from' prompt, use it
		 */
		paqfile = sys;
	} else if(m->arg){
		/*
		 *  a default is supplied when the kernel is made
		 */
		paqfile = m->arg;
	}
}

int
connectembed(void)
{
	int i, p[2];
	Dir *dir;
	char **arg, **argp;

	dir = jehanne_dirstat("/boot/paqfs");
	if(dir == nil)
		return -1;
	jehanne_free(dir);

	dir = jehanne_dirstat(paqfile);
	if(dir == nil || dir->mode & DMDIR)
		return -1;
	jehanne_free(dir);

	jehanne_print("paqfs...");
	if(sys_bind("#0", "/dev", MREPL) < 0)
		fatal("bind #0");
	if(sys_bind("#c", "/dev", MAFTER) < 0)
		fatal("bind #c");
	if(sys_bind("#p", "/proc", MREPL) < 0)
		fatal("bind #p");
	if(jehanne_pipe(p)<0)
		fatal("pipe");
	switch(jehanne_fork()){
	case -1:
		fatal("fork");
	case 0:
		arg = jehanne_malloc((bargc+5)*sizeof(char*));
		argp = arg;
		*argp++ = "/boot/paqfs";
		*argp++ = "-iv";
		*argp++ = paqfile;
		for(i=1; i<bargc; i++)
			*argp++ = bargv[i];
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
