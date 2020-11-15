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
#include <lib9.h>
#include <bio.h>

void
usage(void)
{
	fprint(2, "usage: flip [ -s skip ] [ -n flip ] cmd args ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int skip, flip, i;
	char **nargv;
	
	skip = 0;
	flip = 1;
	ARGBEGIN {
	case 's': skip = atoi(EARGF(usage())); break;
	case 'n': flip = atoi(EARGF(usage())); break;
	default: usage();
	} ARGEND;

	++skip; // cmd must be skipped too

	if(argc < skip + (flip * 2))
		usage();
	
	nargv = malloc(sizeof(char *) * (argc + 1));
	if(nargv == nil)
		sysfatal("malloc: %r");

	for(i = 0; i < argc; ++i){
		if(i < skip){
			nargv[i] = argv[i];
		} else if(i < skip + flip) {
			nargv[i] = argv[argc - i + skip - 1];
		} else if(i > argc - flip - 1){
			nargv[i] = argv[skip + argc - i - 1];
		} else {
			nargv[i] = argv[i];
		}
	}
	sys_exec(*nargv, (const char **)nargv);
	if(**nargv != '/' && strncmp(*nargv, "./", 2) != 0 &&
			strncmp(*nargv, "../", 3) != 0){
		*nargv = smprint("/cmd/%s", *nargv);
		sys_exec(*nargv, (const char **)nargv);
	}
	sysfatal("exec: %r");
}
