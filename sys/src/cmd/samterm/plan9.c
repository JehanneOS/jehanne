/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <envvars.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <plumb.h>
#include "flayer.h"
#include "samterm.h"

enum {
	STACK = 4096,
};

static char exname[64];

void
usage(void)
{
	jehanne_fprint(2, "usage: samterm [-ai]\n");
	threadexitsall("usage");
}

void
getscreen(int argc, char **argv)
{
	char *t;

	ARGBEGIN{
	case 'a':
		autoindent = 1;
		break;
	case 'i':
		spacesindent = 1;
		break;
	default:
		usage();
	}ARGEND

	if(initdraw(panic1, nil, "sam") < 0){
		jehanne_fprint(2, "samterm: initdraw: %r\n");
		threadexitsall("init");
	}
	t = jehanne_getenv("tabstop");
	if(t != nil)
		maxtab = jehanne_strtoul(t, nil, 0);
	jehanne_free(t);
	draw(screen, screen->clipr, display->white, nil, ZP);
}

int
screensize(int *w, int *h)
{
	int fd, n;
	char buf[5*12+1];

	fd = sys_open("/dev/screen", OREAD);
	if(fd < 0)
		return 0;
	n = jehanne_read(fd, buf, sizeof(buf)-1);
	sys_close(fd);
	if (n != sizeof(buf)-1)
		return 0;
	buf[n] = 0;
	if (h) {
		*h = jehanne_atoi(buf+4*12)-jehanne_atoi(buf+2*12);
		if (*h < 0)
			return 0;
	}
	if (w) {
		*w = jehanne_atoi(buf+3*12)-jehanne_atoi(buf+1*12);
		if (*w < 0)
			return 0;
	}
	return 1;
}

int
snarfswap(char *fromsam, int nc, char **tosam)
{
	char *s1;
	int f, n, ss;

	f = sys_open("/dev/snarf", 0);
	if(f < 0)
		return -1;
	ss = SNARFSIZE;
	if(hversion < 2)
		ss = 4096;
	*tosam = s1 = alloc(ss);
	n = jehanne_read(f, s1, ss-1);
	sys_close(f);
	if(n < 0)
		n = 0;
	if (n == 0) {
		*tosam = 0;
		jehanne_free(s1);
	} else
		s1[n] = 0;
	f = jehanne_ocreate("/dev/snarf", OWRITE, 0666);
	if(f >= 0){
		jehanne_write(f, fromsam, nc);
		sys_close(f);
	}
	return n;
}

void
dumperrmsg(int count, int type, int count0, int c)
{
	jehanne_fprint(2, "samterm: host mesg: count %d %ux %ux %ux %s...ignored\n",
		count, type, count0, c, rcvstring());
}

void
removeextern(void)
{
	sys_remove(exname);
}

Readbuf	hostbuf[2];
Readbuf	plumbbuf[2];

void
extproc(void *argv)
{
	Channel *c;
	int i, n, which, *fdp;
	void **arg;

	arg = argv;
	c = arg[0];
	fdp = arg[1];

	i = 0;
	for(;;){
		i = 1-i;	/* toggle */
		n = jehanne_read(*fdp, plumbbuf[i].data, sizeof plumbbuf[i].data);
		if(n <= 0){
			jehanne_fprint(2, "samterm: extern read error: %r\n");
			threadexits("extern");	/* not a fatal error */
		}
		plumbbuf[i].n = n;
		which = i;
		send(c, &which);
	}
}

void
extstart(void)
{
	char buf[32];
	int fd;
	static int p[2];
	static void *arg[2];

	if(jehanne_pipe(p) < 0)
		return;
	jehanne_sprint(exname, "/srv/sam.%s", jehanne_getuser());
	fd = jehanne_ocreate(exname, OWRITE, 0600);
	if(fd < 0){	/* assume existing guy is more important */
    Err:
		sys_close(p[0]);
		sys_close(p[1]);
		return;
	}
	jehanne_sprint(buf, "%d", p[0]);
	if(jehanne_write(fd, buf, jehanne_strlen(buf)) <= 0)
		goto Err;
	sys_close(fd);
	/*
	 * leave p[0] open so if the file is removed the event
	 * library won't get an error
	 */
	plumbc = chancreate(sizeof(int), 0);
	arg[0] = plumbc;
	arg[1] = &p[1];
	proccreate(extproc, arg, STACK);
	jehanne_atexit(removeextern);
}

int
plumbformat(int i)
{
	Plumbmsg *m;
	char *addr, *data, *act;
	int n;

	data = (char*)plumbbuf[i].data;
	m = plumbunpack(data, plumbbuf[i].n);
	if(m == nil)
		return 0;
	n = m->ndata;
	if(n == 0){
		plumbfree(m);
		return 0;
	}
	act = plumblookup(m->attr, "action");
	if(act!=nil && jehanne_strcmp(act, "showfile")!=0){
		/* can't handle other cases yet */
		plumbfree(m);
		return 0;
	}
	addr = plumblookup(m->attr, "addr");
	if(addr){
		if(addr[0] == '\0')
			addr = nil;
		else
			addr = jehanne_strdup(addr);	/* copy to safe storage; we'll overwrite data */
	}
	jehanne_memmove(data, "B ", 2);	/* we know there's enough room for this */
	jehanne_memmove(data+2, m->data, n);
	n += 2;
	if(data[n-1] != '\n')
		data[n++] = '\n';
	if(addr != nil){
		if(n+jehanne_strlen(addr)+1+1 <= READBUFSIZE)
			n += jehanne_sprint(data+n, "%s\n", addr);
		jehanne_free(addr);
	}
	plumbbuf[i].n = n;
	plumbfree(m);
	return 1;
}

void
plumbproc(void *argv)
{
	Channel *c;
	int i, n, which, *fdp;
	void **arg;

	arg = argv;
	c = arg[0];
	fdp = arg[1];

	i = 0;
	for(;;){
		i = 1-i;	/* toggle */
		n = jehanne_read(*fdp, plumbbuf[i].data, READBUFSIZE);
		if(n <= 0){
			jehanne_fprint(2, "samterm: plumb read error: %r\n");
			threadexits("plumb");	/* not a fatal error */
		}
		plumbbuf[i].n = n;
		if(plumbformat(i)){
			which = i;
			send(c, &which);
		}
	}
}

int
plumbstart(void)
{
	static int fd;
	static void *arg[2];

	plumbfd = plumbopen("send", OWRITE|OCEXEC);	/* not open is ok */
	fd = plumbopen("edit", OREAD|OCEXEC);
	if(fd < 0)
		return -1;
	plumbc = chancreate(sizeof(int), 0);
	if(plumbc == nil){
		sys_close(fd);
		return -1;
	}
	arg[0] =plumbc;
	arg[1] = &fd;
	proccreate(plumbproc, arg, STACK);
	return 1;
}

void
hostproc(void *arg)
{
	Channel *c;
	int i, n, which;

	c = arg;

	i = 0;
	for(;;){
		i = 1-i;	/* toggle */
		n = jehanne_read(0, hostbuf[i].data, sizeof hostbuf[i].data);
		if(n <= 0){
			if(n==0){
				if(exiting)
					threadexits(nil);
				jehanne_werrstr("unexpected eof");
			}
			jehanne_fprint(2, "samterm: host read error: %r\n");
			threadexitsall("host");
		}
		hostbuf[i].n = n;
		which = i;
		send(c, &which);
	}
}

void
hoststart(void)
{
	hostc = chancreate(sizeof(int), 0);
	proccreate(hostproc, hostc, STACK);
}
