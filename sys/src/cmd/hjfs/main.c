/* Copyright (c) 20XX 9front
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <u.h>
#include <lib9.h>
#include <libsec.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

char Eio[] = "i/o error";
char Enotadir[] = "not a directory";
char Enoent[] = "not found";
char Einval[] = "invalid operation";
char Eperm[] = "permission denied";
char Eexists[] = "file exists";
char Elocked[] = "file locked";

int mainstacksize = 65536;

void*
emalloc(int c)
{
	void *v;
	
	v = mallocz(c, 1);
	if(v == 0)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc());
	return v;
}

void*
erealloc(void *v, int c)
{
	v = realloc(v, c);
	if(v == 0 && c != 0)
		sysfatal("realloc: %r");
	setrealloctag(v, getcallerpc());
	return v;
}

char*
estrdup(char *s)
{
	s = strdup(s);
	if(s == 0)
		sysfatal("strdup: %r");
	setmalloctag(s, getcallerpc());
	return s;
}

ThrData *
getthrdata(void)
{
	ThrData **v;
	
	v = (ThrData **) threaddata();
	if(*v == nil){
		*v = emalloc(sizeof(**v));
		(*v)->resp = chancreate(sizeof(void *), 0);
	}
	return *v;
}

Fs *fsmain;

int
dprint(char *fmt, ...)
{
	static char buf[2048];
	static QLock lk;
	va_list va;
	int rc;

	qlock(&lk);
	va_start(va, fmt);
	snprint(buf, 2048, "hjfs: %s", fmt);
	rc = vfprint(2, buf, va);
	va_end(va);
	qunlock(&lk);
	return rc;
}

static void
syncproc(void * _)
{
	for(;;){
		sync(0);
		sleep(SYNCINTERVAL);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-rsS] [-m mem] [-n service] [-a announce-string]... -f dev\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Dev *d;
	static char *nets[8];
	char *file, *service;
	int doream, flags, stdio, nbuf, netc;

	netc = 0;
	doream = 0;
	stdio = 0;
	flags = FSNOAUTH;
	service = "hjfs";
	file = nil;
	nbuf = 1000;
	ARGBEGIN {
	case 'A': flags &= ~FSNOAUTH; break;
	case 'r': doream++; break;
	case 'S': flags |= FSNOPERM | FSCHOWN; break;
	case 's': stdio++; break;
	case 'f': file = estrdup(EARGF(usage())); break;
	case 'n': service = estrdup(EARGF(usage())); break;
	case 'm':
		nbuf = muldiv(atoi(EARGF(usage())), 1048576, sizeof(Buf));
		if(nbuf < 10)
			nbuf = 10;
		break;
	case 'a':
		if(netc >= nelem(nets)-1){
			fprint(2, "%s: too many networks to announce\n", argv0);
			exits("too many nets");
		}
		nets[netc++] = estrdup(EARGF(usage()));
		break;
	default: usage();
	} ARGEND;
	sys_rfork(RFNOTEG);
	bufinit(nbuf);
	if(file == nil)
		sysfatal("no default file");
	if(argc != 0)
		usage();
	d = newdev(file);
	if(d == nil)
		sysfatal("newdev: %r");
	fsmain = initfs(d, doream, flags);
	if(fsmain == nil)
		sysfatal("fsinit: %r");
	initcons(service);
	proccreate(syncproc, nil, mainstacksize);
	start9p(service, nets, stdio);
	threadexits(nil);
}

void
shutdown(void)
{
	wlock(fsmain);
	sync(1);
	dprint("ending\n");
	sleep(1000);
	sync(1);
	threadexitsall(nil);
}
