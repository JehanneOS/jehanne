/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
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
#include <9P2000.h>

#include "console.h"

#define WAIT_FOR(v) while(sys_rendezvous(&v, (void*)0x12345) == (void*)~0)
#define PROVIDE(v) while(sys_rendezvous(&v, (void*)0x11111) == (void*)~0)

/* debugging */
int debugging;
extern void (*__assert)(char*);
static int
debugnotes(void *v, char *s)
{
	debug("%d: noted: %s\n", getpid(), s);
	return 0;
}
static void
traceassert(char*a)
{
	fprint(2, "assert failed: %s, %#p %#p %#p %#p %#p %#p %#p %#p %#p %#p\n", a,
		__builtin_return_address(2),
		__builtin_return_address(3),
		__builtin_return_address(4),
		__builtin_return_address(5),
		__builtin_return_address(6),
		__builtin_return_address(7),
		__builtin_return_address(8),
		__builtin_return_address(9),
		__builtin_return_address(10),
		__builtin_return_address(11)
	);
	exits(a);
}
void
enabledebug(const char *file)
{
	int fd;

	if (!debugging) {
		if((fd = sys_open(file, OCEXEC|OTRUNC|OWRITE)) < 0){
			debug("open: %r\n");
			if((fd = ocreate(file, OCEXEC|OWRITE, 0666)) < 0)
				sysfatal("create %r");
		}
		dup(fd, 2);
		sys_close(fd);
		__assert = traceassert;
		if(!atnotify(debugnotes, 1)){
			fprint(2, "atnotify: %r\n");
			exits("atnotify");
		}
		fmtinstall('F', fcallfmt);
	}
	++debugging;
}
void
debug(const char *fmt, ...)
{
	va_list arg;

	if (debugging) {
		va_start(arg, fmt);
		vfprint(2, fmt, arg);
		va_end(arg);
	}
}

/* process management */
/* start the relevant services
 *
 * assumes that
 *  - fd 0 can be read by inputFilter
 *  - fd 1 can be written by outputFilter
 *  - fd 2 can receive debug info (if debugging)
 *
 * returns the fd to mount
 */
int
servecons(StreamFilter inputFilter, StreamFilter outputFilter, int *devmnt)
{
	int pid, input, output, fs, mnt;

	pid = getpid();

	sys_rfork(RFNAMEG);

	debug("%s %d: started, linecontrol = %d, blind = %d\n", argv0, pid, linecontrol, blind);

	fs = fsinit(&mnt, devmnt);

	if(!debugging)
		sys_close(2);

	/* start the file system */
	switch(sys_rfork(RFPROC|RFMEM|RFNOWAIT|RFCENVG|RFNOTEG|RFCNAMEG|RFFDG)){
		case -1:
			sysfatal("rfork (file server)");
			break;
		case 0:
			sys_close(0);
			sys_close(1);
			sys_close(mnt);
			PROVIDE(fs);
			sys_rfork(RFREND);
			fsserve(fs);
			break;
		default:
			break;
	}

	WAIT_FOR(fs);
	sys_close(fs);

	/* start output device writer */
	debug("%s %d: starting output device writer\n", argv0, pid);
	switch(sys_rfork(RFPROC|RFMEM|RFNOWAIT|RFNOTEG|RFFDG|RFNAMEG)){
		case -1:
			sysfatal("rfork (output writer): %r");
			break;
		case 0:
			if(sys_mount(mnt, -1, "/dev", MBEFORE, "", *devmnt) == -1)
				sysfatal("mount (output writer): %r");
			if((output = sys_open("/dev/gconsout", OREAD)) == -1)
				sysfatal("open /dev/gconsout: %r");
			sys_close(0);
			PROVIDE(output);
			sys_rfork(RFCENVG|RFCNAMEG|RFREND);
			outputFilter(output, 1);
			break;
		default:
			break;
	}

	/* start input device reader */
	debug("%s %d: starting input device reader\n", argv0, pid);
	switch(sys_rfork(RFPROC|RFMEM|RFNOWAIT|RFNOTEG|RFFDG|RFNAMEG)){
		case -1:
			sysfatal("rfork (input reader): %r");
			break;
		case 0:
			if(sys_mount(mnt, -1, "/dev", MBEFORE, "", *devmnt) == -1)
				sysfatal("mount (input reader): %r");
			if((input = sys_open("/dev/gconsin", OWRITE)) == -1)
				sysfatal("open /dev/gconsin: %r");
			sys_close(1);
			PROVIDE(input);
			sys_rfork(RFCENVG|RFCNAMEG|RFREND);
			inputFilter(0, input);
			break;
		default:
			break;
	}

	WAIT_FOR(input);
	WAIT_FOR(output);
	sys_close(0);
	sys_close(1);

	return mnt;
}
void
post(char *srv, int fd)
{
	int f;
	char buf[128];

	fprint(2, "post %s...\n", srv);
	sprint(buf, "#s/%s", srv);
	f = ocreate(buf, OWRITE, 0666);
	if(f < 0)
		sysfatal("ocreate(%s)", srv);
	sprint(buf, "%d", fd);
	if(jehanne_write(f, buf, strlen(buf)) != strlen(buf))
		sysfatal("write");
	sys_close(f);
}
