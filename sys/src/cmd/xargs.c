/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include <u.h>
#include <lib9.h>
#include <bio.h>

void
usage(void)
{
	fprint(2, "usage: xargs [ -n lines ] [ -p procs ] args ...\n");
	exits("usage");
}

void
dowait(void)
{
	while(waitpid() != -1)
		;
}

void
main(int argc, char **argv)
{
	int lines, procs, i, j, run;
	char **nargv, **args, **p;
	static Biobuf bp;
	
	lines = 10;
	procs = 1;
	ARGBEGIN {
	case 'n': lines = atoi(EARGF(usage())); break;
	case 'p': procs = atoi(EARGF(usage())); break;
	default: usage();
	} ARGEND;
	if(argc < 1)
		usage();
	
	nargv = malloc(sizeof(char *) * (argc + lines + 1));
	if(nargv == nil)
		sysfatal("malloc: %r");
	memcpy(nargv, argv, sizeof(char *) * argc);
	args = nargv + argc;
	if(Binit(&bp, 0, OREAD) < 0)
		sysfatal("Binit: %r");
	atexit(dowait);
	for(j = 0, run = 1; run; j++){
		if(j >= procs)
			waitpid();
		memset(args, 0, sizeof(char *) * (lines + 1));
		for(i = 0; i < lines; i++)
			if((args[i] = Brdstr(&bp, '\n', 1)) == nil){
				if(i == 0)
					exits(nil);
				run = 0;
				break;
			}
		switch(fork()){
		case -1:
			sysfatal("fork: %r");
		case 0:
			sys_exec(*nargv, nargv);
			if(**nargv != '/' && strncmp(*nargv, "./", 2) != 0 &&
					strncmp(*nargv, "../", 3) != 0){
				*nargv = smprint("/cmd/%s", *nargv);
				sys_exec(*nargv, nargv);
			}
			sysfatal("exec: %r");
		}
		for(p = args; *p; p++)
			free(*p);
	}
	exits(nil);
}
