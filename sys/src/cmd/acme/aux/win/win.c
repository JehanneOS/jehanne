/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <thread.h>
#include "dat.h"

Window*
newwindow(void)
{
	char buf[12];
	Window *w;

	w = emalloc(sizeof(Window));
	w->ctl = sys_open("/mnt/wsys/new/ctl", ORDWR|OCEXEC);
	if(w->ctl<0 || jehanne_read(w->ctl, buf, 12)!=12)
		error("can't open window ctl file: %r");
	ctlprint(w->ctl, "noscroll\n");
	w->id = atoi(buf);
	w->event = winopenfile(w, "event");
	w->addr = winopenfile(w, "addr");
	w->body = winopenfile(w, "body");
	w->data = winopenfile(w, "data");
	w->cevent = chancreate(sizeof(Event*), 0);
	return w;
}

void
winsetdump(Window *w, char *dir, char *cmd)
{
	if(dir != nil)
		ctlprint(w->ctl, "dumpdir %s\n", dir);
	if(cmd != nil)
		ctlprint(w->ctl, "dump %s\n", cmd);
}

void
wineventproc(void *v)
{
	Window *w;
	int i;

	w = v;
	for(i=0; ; i++){
		if(i >= NEVENT)
			i = 0;
		wingetevent(w, &w->e[i]);
		sendp(w->cevent, &w->e[i]);
	}
}

int
winopenfile(Window *w, char *f)
{
	char buf[64];
	int fd;

	sprint(buf, "/mnt/wsys/%d/%s", w->id, f);
	fd = sys_open(buf, ORDWR|OCEXEC);
	if(fd < 0)
		error("can't open window file %s: %r", f);
	return fd;
}

void
wintagwrite(Window *w, char *s, int n)
{
	int fd;

	fd = winopenfile(w, "tag");
	if(jehanne_write(fd, s, n) != n)
		error("tag jehanne_write: %r");
	sys_close(fd);
}

void
winname(Window *w, char *s)
{
	ctlprint(w->ctl, "name %s\n", s);
}

int
wingetec(Window *w)
{
	if(w->nbuf == 0){
		w->nbuf = jehanne_read(w->event, w->buf, sizeof w->buf);
		if(w->nbuf <= 0){
			/* probably because window has exited, and only called by wineventproc, so just shut down */
			threadexits(nil);
		}
		w->bufp = w->buf;
	}
	w->nbuf--;
	return *w->bufp++;
}

int
wingeten(Window *w)
{
	int n, c;

	n = 0;
	while('0'<=(c=wingetec(w)) && c<='9')
		n = n*10+(c-'0');
	if(c != ' ')
		error("event number syntax");
	return n;
}

int
wingeter(Window *w, char *buf, int *nb)
{
	Rune r;
	int n;

	r = wingetec(w);
	buf[0] = r;
	n = 1;
	if(r >= Runeself) {
		while(!fullrune(buf, n))
			buf[n++] = wingetec(w);
		chartorune(&r, buf);
	} 
	*nb = n;
	return r;
}

void
wingetevent(Window *w, Event *e)
{
	int i, nb;

	e->c1 = wingetec(w);
	e->c2 = wingetec(w);
	e->q0 = wingeten(w);
	e->q1 = wingeten(w);
	e->flag = wingeten(w);
	e->nr = wingeten(w);
	if(e->nr > EVENTSIZE)
		error("event string too long");
	e->nb = 0;
	for(i=0; i<e->nr; i++){
		e->r[i] = wingeter(w, e->b+e->nb, &nb);
		e->nb += nb;
	}
	e->r[e->nr] = 0;
	e->b[e->nb] = 0;
	if(wingetec(w) != '\n')
		error("event syntax error");
}

void
winwriteevent(Window *w, Event *e)
{
	fprint(w->event, "%c%c%d %d\n", e->c1, e->c2, e->q0, e->q1);
}

static int
nrunes(char *s, int nb)
{
	int i, n;
	Rune r;

	n = 0;
	for(i=0; i<nb; n++)
		i += chartorune(&r, s+i);
	return n;
}

int
winread(Window *w, uint32_t q0, uint32_t q1, char *data)
{
	int m, n, nr, nb;
	char buf[256];

	if(w->addr < 0)
		w->addr = winopenfile(w, "addr");
	if(w->data < 0)
		w->data = winopenfile(w, "data");
	m = q0;
	nb = 0;
	while(m < q1){
		n = sprint(buf, "#%d", m);
		if(jehanne_write(w->addr, buf, n) != n)
			error("error writing addr: %r");
		n = jehanne_read(w->data, buf, sizeof buf);
		if(n < 0)
			error("reading data: %r");
		nr = nrunes(buf, n);
		while(m+nr >q1){
			do; while(n>0 && (buf[--n]&0xC0)==0x80);
			--nr;
		}
		if(n == 0)
			break;
		memmove(data, buf, n);
		nb += n;
		data += n;
		*data = 0;
		m += nr;
	}
	return nb;
}

void
windormant(Window *w)
{
	if(w->addr >= 0){
		sys_close(w->addr);
		w->addr = -1;
	}
	if(w->body >= 0){
		sys_close(w->body);
		w->body = -1;
	}
	if(w->data >= 0){
		sys_close(w->data);
		w->data = -1;
	}
}

int
windel(Window *w, int sure)
{
	if(sure)
		jehanne_write(w->ctl, "delete\n", 7);
	else if(jehanne_write(w->ctl, "del\n", 4) != 4)
		return 0;
	/* event proc will die due to read error from event file */
	windormant(w);
	sys_close(w->ctl);
	w->ctl = -1;
	sys_close(w->event);
	w->event = -1;
	return 1;
}

void
winclean(Window *w)
{
	ctlprint(w->ctl, "clean\n");
}

int
winsetaddr(Window *w, char *addr, int errok)
{
	if(w->addr < 0)
		w->addr = winopenfile(w, "addr");
	if(jehanne_write(w->addr, addr, strlen(addr)) < 0){
		if(!errok)
			error("error writing addr(%s): %r", addr);
		return 0;
	}
	return 1;
}

int
winselect(Window *w, char *addr, int errok)
{
	if(winsetaddr(w, addr, errok)){
		ctlprint(w->ctl, "dot=addr\n");
		return 1;
	}
	return 0;
}
