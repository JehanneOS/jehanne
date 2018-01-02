/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"../ip/ip.h"

enum {
	Nlog		= 16*1024,
};

/*
 *  action log
 */
struct Netlog {
	Lock	l;
	int	opens;
	char*	buf;
	char	*end;
	char	*rptr;
	int	len;

	int	logmask;			/* mask of things to debug */
	uint8_t	iponly[IPaddrlen];		/* ip address to print debugging for */
	int	iponlyset;

	QLock	ql;
	Rendez	rend;
};

typedef struct Netlogflag {
	char*	name;
	int	mask;
} Netlogflag;

static Netlogflag flags[] =
{
	{ "ppp",	Logppp, },
	{ "ip",		Logip, },
	{ "fs",		Logfs, },
	{ "tcp",	Logtcp, },
	{ "il",		Logil, },
	{ "icmp",	Logicmp, },
	{ "udp",	Logudp, },
	{ "compress",	Logcompress, },
	{ "ilmsg",	Logil|Logilmsg, },
	{ "gre",	Loggre, },
	{ "tcpwin",	Logtcp|Logtcpwin, },
	{ "tcprxmt",	Logtcp|Logtcprxmt, },
	{ "udpmsg",	Logudp|Logudpmsg, },
	{ "ipmsg",	Logip|Logipmsg, },
	{ "esp",	Logesp, },
	{ nil,		0, },
};

char Ebadnetctl[] = "too few arguments for netlog control message";

enum
{
	CMset,
	CMclear,
	CMonly,
};

static
Cmdtab routecmd[] = {
	CMset,		"set",		0,
	CMclear,	"clear",	0,
	CMonly,		"only",		0,
};

void
netloginit(Fs *f)
{
	f->alog = smalloc(sizeof(Netlog));
}

void
netlogopen(Fs *f)
{
	lock(&f->alog->l);
	if(waserror()){
		unlock(&f->alog->l);
		nexterror();
	}
	if(f->alog->opens == 0){
		if(f->alog->buf == nil){
			f->alog->buf = jehanne_malloc(Nlog);
			if(f->alog->buf == nil)
				error(Enomem);
		}
		f->alog->rptr = f->alog->buf;
		f->alog->end = f->alog->buf + Nlog;
	}
	f->alog->opens++;
	poperror();
	unlock(&f->alog->l);
}

void
netlogclose(Fs *f)
{
	lock(&f->alog->l);
	f->alog->opens--;
	if(f->alog->opens == 0){
		jehanne_free(f->alog->buf);
		f->alog->buf = nil;
	}
	unlock(&f->alog->l);
}

static int
netlogready(void *a)
{
	Fs *f = a;

	return f->alog->len;
}

long
netlogread(Fs *f, void *a, uint32_t _1, long n)
{
	int i, d;
	char *p, *rptr;

	qlock(&f->alog->ql);
	if(waserror()){
		qunlock(&f->alog->ql);
		nexterror();
	}

	for(;;){
		lock(&f->alog->l);
		if(f->alog->len){
			if(n > f->alog->len)
				n = f->alog->len;
			d = 0;
			rptr = f->alog->rptr;
			f->alog->rptr += n;
			if(f->alog->rptr >= f->alog->end){
				d = f->alog->rptr - f->alog->end;
				f->alog->rptr = f->alog->buf + d;
			}
			f->alog->len -= n;
			unlock(&f->alog->l);

			i = n-d;
			p = a;
			jehanne_memmove(p, rptr, i);
			jehanne_memmove(p+i, f->alog->buf, d);
			break;
		}
		else
			unlock(&f->alog->l);

		sleep(&f->alog->rend, netlogready, f);
	}

	poperror();
	qunlock(&f->alog->ql);

	return n;
}

void
netlogctl(Fs *f, char* s, int n)
{
	int i, set;
	Netlogflag *fp;
	Cmdbuf *cb;
	Cmdtab *ct;

	cb = parsecmd(s, n);
	if(waserror()){
		jehanne_free(cb);
		nexterror();
	}

	if(cb->nf < 2)
		error(Ebadnetctl);

	ct = lookupcmd(cb, routecmd, nelem(routecmd));
	switch(ct->index){
	case CMset:
		set = 1;
		break;

	case CMclear:
		set = 0;
		break;

	case CMonly:
		parseip(f->alog->iponly, cb->f[1]);
		if(ipcmp(f->alog->iponly, IPnoaddr) == 0)
			f->alog->iponlyset = 0;
		else
			f->alog->iponlyset = 1;
		poperror();
		jehanne_free(cb);
		return;

	default:
		SET(set);
		cmderror(cb, "unknown ip control message");
	}

	for(i = 1; i < cb->nf; i++){
		for(fp = flags; fp->name; fp++)
			if(jehanne_strcmp(fp->name, cb->f[i]) == 0)
				break;
		if(fp->name == nil)
			continue;
		if(set)
			f->alog->logmask |= fp->mask;
		else
			f->alog->logmask &= ~fp->mask;
	}

	poperror();
	jehanne_free(cb);
}

void
netlog(Fs *f, int mask, char *fmt, ...)
{
	char buf[256], *t, *fp;
	int i, n;
	va_list arg;

	if(!(f->alog->logmask & mask))
		return;

	if(f->alog->opens == 0)
		return;

	va_start(arg, fmt);
	n = jehanne_vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	lock(&f->alog->l);
	i = f->alog->len + n - Nlog;
	if(i > 0){
		f->alog->len -= i;
		f->alog->rptr += i;
		if(f->alog->rptr >= f->alog->end)
			f->alog->rptr = f->alog->buf + (f->alog->rptr - f->alog->end);
	}
	t = f->alog->rptr + f->alog->len;
	fp = buf;
	f->alog->len += n;
	while(n-- > 0){
		if(t >= f->alog->end)
			t = f->alog->buf + (t - f->alog->end);
		*t++ = *fp++;
	}
	unlock(&f->alog->l);

	wakeup(&f->alog->rend);
}
