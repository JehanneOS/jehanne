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
#include <envvars.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <9P2000.h>
#include <plumb.h>
#include "dat.h"
#include "fns.h"

/*
 *  WASHINGTON (AP) - The Food and Drug Administration warned
 * consumers Wednesday not to use ``Rio'' hair relaxer products
 * because they may cause severe hair loss or turn hair green....
 *    The FDA urged consumers who have experienced problems with Rio
 * to notify their local FDA office, local health department or the
 * company at 1‑800‑543‑3002.
 */

void		resize(void);
void		move(void);
void		delete(void);
void		hide(void);
void		unhide(int);
void		newtile(int);
Image*	sweep(void);
Image*	bandsize(Window*);
Image*	drag(Window*);
void		resized(void);
Channel	*exitchan;	/* chan(int) */
Channel	*winclosechan; /* chan(Window*); */
Channel	*kbdchan;	/* chan(char*); */
Rectangle	viewr;
int		threadrforkflag = 0;	/* should be RFENVG but that hides rio from plumber */

void	mousethread(void*);
void	keyboardthread(void*);
void	winclosethread(void*);
void	deletethread(void*);
void	initcmd(void*);
Channel* initkbd(void);

char		*fontname;

enum
{
	New,
	Reshape,
	Move,
	Delete,
	Hide,
	Exit,
};

enum
{
	Cut,
	Paste,
	Snarf,
	Plumb,
	Look,
	Send,
	Scroll,
};

char		*menu2str[] = {
 [Cut]		"cut",
 [Paste]		"paste",
 [Snarf]		"snarf",
 [Plumb]		"plumb",
 [Look]		"look",
 [Send]		"send",
 [Scroll]		"scroll",
			nil
};

Menu menu2 =
{
	menu2str
};

int	Hidden = Exit+1;

char		*menu3str[100] = {
 [New]		"New",
 [Reshape]	"Resize",
 [Move]		"Move",
 [Delete]		"Delete",
 [Hide]		"Hide",
 [Exit]		"Exit",
			nil
};

Menu menu3 =
{
	menu3str
};

char *rcargv[] = { "rc", "-i", nil };
char *kbdargv[] = { "rc", "-c", nil, nil };

int errorshouldabort = 0;

void
derror(Display* _, char *errorstr)
{
	error(errorstr);
}

void
usage(void)
{
	fprint(2, "usage: rio [-b] [-f font] [-i initcmd] [-k kbdcmd] [-s]\n");
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	char *initstr, *kbdin, *s;
	char buf[256];
	Image *i;
	Rectangle r;

	if(strstr(argv[0], ".out") == nil){
		menu3str[Exit] = nil;
		Hidden--;
	}
	initstr = nil;
	kbdin = nil;
	maxtab = 0;
	ARGBEGIN{
	case 'b':
		reverse = ~0xFF;
		break;
	case 'f':
		fontname = EARGF(usage());
		break;
	case 'i':
		initstr = EARGF(usage());
		break;
	case 'k':
		if(kbdin != nil)
			usage();
		kbdin = EARGF(usage());
		break;
	case 's':
		scrolling = TRUE;
		break;
	case 'D':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	if(getwd(buf, sizeof buf) <= 0)
		startdir = estrdup(".");
	else
		startdir = estrdup(buf);
	if(fontname == nil)
		fontname = getenv("font");
	if(fontname == nil)
		fontname = "/lib/font/bit/fixed/unicode.6x13.font";
	s = getenv("tabstop");
	if(s != nil)
		maxtab = strtol(s, nil, 0);
	if(maxtab == 0)
		maxtab = 4;
	free(s);

	s = getenv(ENV_CPUTYPE);
	if(s){
		snprint(buf, sizeof(buf), "/arch/%s/aux/rio", s);
		sys_bind(buf, "/cmd", MBEFORE);
		free(s);
	}
	sys_bind("/arch/rc/aux/rio", "/cmd", MBEFORE);

	/* check font before barging ahead */
	if(access(fontname, 0) < 0){
		fprint(2, "rio: can't access %s: %r\n", fontname);
		exits("font open");
	}
	putenv("font", fontname);

	snarffd = sys_open("/dev/snarf", OREAD|OCEXEC);
	gotscreen = access("/dev/screen", AEXIST)==0;

	if(geninitdraw(nil, derror, nil, "rio", nil, Refnone) < 0){
		fprint(2, "rio: can't open display: %r\n");
		exits("display open");
	}
	iconinit();

	exitchan = chancreate(sizeof(int), 0);
	winclosechan = chancreate(sizeof(Window*), 0);
	deletechan = chancreate(sizeof(char*), 0);

	view = screen;
	viewr = view->r;
	mousectl = initmouse(nil, screen);
	if(mousectl == nil)
		error("can't find mouse");
	mouse = mousectl;
	kbdchan = initkbd();
	if(kbdchan == nil)
		error("can't find keyboard");
	wscreen = allocscreen(screen, background, 0);
	if(wscreen == nil)
		error("can't allocate screen");
	draw(view, viewr, background, nil, ZP);
	flushimage(display, 1);

	timerinit();
	threadcreate(keyboardthread, nil, STACK);
	threadcreate(mousethread, nil, STACK);
	threadcreate(winclosethread, nil, STACK);
	threadcreate(deletethread, nil, STACK);
	filsys = filsysinit(xfidinit());

	if(filsys == nil)
		fprint(2, "rio: can't create file system server: %r\n");
	else{
		errorshouldabort = 1;	/* suicide if there's trouble after this */
		if(initstr)
			proccreate(initcmd, initstr, STACK);
		if(kbdin){
			kbdargv[2] = kbdin;
			r = screen->r;
			r.min.y = r.max.y-Dy(r)/3;
			i = allocwindow(wscreen, r, Refbackup, DNofill);
			wkeyboard = new(i, FALSE, scrolling, 0, nil, "/cmd/rc", kbdargv);
			if(wkeyboard == nil)
				error("can't create keyboard window");
		}
		threadnotify(shutdown, 1);
		recv(exitchan, nil);
	}
	killprocs();
	threadexitsall(nil);
}

/*
 * /dev/snarf updates when the file is closed, so we must open our own
 * fd here rather than use snarffd
 */
void
putsnarf(void)
{
	int fd, i, n;

	if(snarffd<0 || nsnarf==0)
		return;
	fd = sys_open("/dev/snarf", OWRITE);
	if(fd < 0)
		return;
	/* snarf buffer could be huge, so fprint will truncate; do it in blocks */
	for(i=0; i<nsnarf; i+=n){
		n = nsnarf-i;
		if(n >= 256)
			n = 256;
		if(fprint(fd, "%.*S", n, snarf+i) < 0)
			break;
	}
	sys_close(fd);
}

void
getsnarf(void)
{
	int i, n, nb, nulls;
	char *s, *sn;

	if(snarffd < 0)
		return;
	sn = nil;
	i = 0;
	sys_seek(snarffd, 0, 0);
	for(;;){
		if(i > MAXSNARF)
			break;
		if((s = realloc(sn, i+1024+1)) == nil)
			break;
		sn = s;
		if((n = jehanne_read(snarffd, sn+i, 1024)) <= 0)
			break;
		i += n;
	}
	if(i == 0)
		return;
	sn[i] = 0;
	if((snarf = runerealloc(snarf, i+1)) != nil)
		cvttorunes(sn, i, snarf, &nb, &nsnarf, &nulls);
	free(sn);
}

void
initcmd(void *arg)
{
	char *cmd;

	cmd = arg;
	sys_rfork(RFENVG|RFFDG|RFNOTEG|RFNAMEG);
	procexecl(nil, "/cmd/rc", "rc", "-c", cmd, nil);
	fprint(2, "rio: exec failed: %r\n");
	exits("exec");
}

char *oknotes[] =
{
	"delete",
	"hangup",
	"kill",
	"exit",
	nil
};

int
shutdown(void * _, char *msg)
{
	int i;
	static Lock shutdownlk;

	killprocs();
	for(i=0; oknotes[i]; i++)
		if(strncmp(oknotes[i], msg, strlen(oknotes[i])) == 0){
			jehanne_lock(&shutdownlk);	/* only one can threadexitsall */
			threadexitsall(msg);
		}
	fprint(2, "rio %d: abort: %s\n", getpid(), msg);
	abort();
	exits(msg);
	return 0;
}

void
killprocs(void)
{
	int i;

	for(i=0; i<nwindow; i++)
		if(window[i]->notefd >= 0)
			jehanne_write(window[i]->notefd, "hangup", 6);
}

void
keyboardthread(void* _)
{
	char *s;

	threadsetname("keyboardthread");

	while(s = recvp(kbdchan)){
		if(*s == 'k' || *s == 'K')
			shiftdown = utfrune(s+1, Kshift) != nil;
		if(input == nil || sendp(input->ck, s) <= 0)
			free(s);
	}
}

int
inborder(Rectangle r, Point xy)
{
	return ptinrect(xy, r) && !ptinrect(xy, insetrect(r, Selborder));
}

Rectangle
whichrect(Rectangle r, Point p, int which)
{
	switch(which){
	case 0:	/* top left */
		r = Rect(p.x, p.y, r.max.x, r.max.y);
		break;
	case 2:	/* top right */
		r = Rect(r.min.x, p.y, p.x+1, r.max.y);
		break;
	case 6:	/* bottom left */
		r = Rect(p.x, r.min.y, r.max.x, p.y+1);
		break;
	case 8:	/* bottom right */
		r = Rect(r.min.x, r.min.y, p.x+1, p.y+1);
		break;
	case 1:	/* top edge */
		r = Rect(r.min.x, p.y, r.max.x, r.max.y);
		break;
	case 5:	/* right edge */
		r = Rect(r.min.x, r.min.y, p.x+1, r.max.y);
		break;
	case 7:	/* bottom edge */
		r = Rect(r.min.x, r.min.y, r.max.x, p.y+1);
		break;
	case 3:		/* left edge */
		r = Rect(p.x, r.min.y, r.max.x, r.max.y);
		break;
	}
	return canonrect(r);
}

int
portion(int x, int lo, int hi)
{
	x -= lo;
	hi -= lo;
	if(hi < 20)
		return x > 0 ? 2 : 0;
	if(x < 20)
		return 0;
	if(x > hi-20)
		return 2;
	return 1;
}

int
whichcorner(Rectangle r, Point p)
{
	int i, j;

	i = portion(p.x, r.min.x, r.max.x);
	j = portion(p.y, r.min.y, r.max.y);
	return 3*j+i;
}

/* thread to allow fsysproc to synchronize window closing with main proc */
void
winclosethread(void* _)
{
	Window *w;

	threadsetname("winclosethread");
	for(;;){
		w = recvp(winclosechan);
		wclose(w);
	}
}

/* thread to make Deleted windows that the client still holds disappear offscreen after an interval */
void
deletethread(void* _)
{
	char *s;
	Image *i;

	threadsetname("deletethread");
	for(;;){
		s = recvp(deletechan);
		i = namedimage(display, s);
		if(i != nil){
			/* move it off-screen to hide it, since client is slow in letting it go */
			originwindow(i, i->r.min, view->r.max);
			freeimage(i);
			flushimage(display, 1);
		}
		free(s);
	}
}

void
deletetimeoutproc(void *v)
{
	char *s;

	s = v;
	sleep(750);	/* remove window from screen after 3/4 of a second */
	sendp(deletechan, s);
}

/*
 * Button 6 - keyboard toggle - has been pressed.
 * Send event to keyboard, wait for button up, send that.
 * Note: there is no coordinate translation done here; this
 * is just about getting button 6 to the keyboard simulator.
 */
void
keyboardhide(void)
{
	send(wkeyboard->mc.c, mouse);
	do
		readmouse(mousectl);
	while(mouse->buttons & (1<<5));
	send(wkeyboard->mc.c, mouse);
}

void
mousethread(void* _)
{
	int sending, inside, scrolling, moving;
	Window *w, *winput;
	Image *i;
	Point xy;
	Mouse tmp;
	enum {
		MReshape,
		MMouse,
		NALT
	};
	static Alt alts[NALT+1];

	threadsetname("mousethread");
	sending = FALSE;
	scrolling = FALSE;

	alts[MReshape].c = mousectl->resizec;
	alts[MReshape].v = nil;
	alts[MReshape].op = CHANRCV;
	alts[MMouse].c = mousectl->c;
	alts[MMouse].v = &mousectl->Mouse;
	alts[MMouse].op = CHANRCV;
	alts[NALT].op = CHANEND;

	for(;;)
	    switch(alt(alts)){
		case MReshape:
			resized();
			break;
		case MMouse:
			if(wkeyboard!=nil && (mouse->buttons & (1<<5))){
				keyboardhide();
				break;
			}
		Again:
			moving = FALSE;
			winput = input;
			/* override everything for the keyboard window */
			if(wkeyboard!=nil && ptinrect(mouse->xy, wkeyboard->screenr)){
				/* make sure it's on top; this call is free if it is */
				wtopme(wkeyboard);
				winput = wkeyboard;
			}
			if(winput!=nil && !winput->deleted && winput->i!=nil){
				/* convert to logical coordinates */
				xy.x = mouse->xy.x + (winput->i->r.min.x-winput->screenr.min.x);
				xy.y = mouse->xy.y + (winput->i->r.min.y-winput->screenr.min.y);

				/* the up and down scroll buttons are not subject to the usual rules */
				if((mouse->buttons&(8|16)) && !winput->mouseopen)
					goto Sending;

				inside = ptinrect(mouse->xy, insetrect(winput->screenr, Selborder));
				if(winput->mouseopen)
					scrolling = FALSE;
				else if(scrolling)
					scrolling = mouse->buttons;
				else
					scrolling = mouse->buttons && ptinrect(xy, winput->scrollr);
				/* topped will be zero or less if window has been bottomed */
				if(sending == FALSE && !scrolling && inborder(winput->screenr, mouse->xy) && winput->topped>0)
					moving = TRUE;
				else if(inside && (scrolling || winput->mouseopen || (mouse->buttons&1)))
					sending = TRUE;
			}else
				sending = FALSE;
			if(sending){
			Sending:
				wsetcursor(winput, FALSE);
				if(mouse->buttons == 0)
					sending = FALSE;
				tmp = mousectl->Mouse;
				tmp.xy = xy;
				send(winput->mc.c, &tmp);
				continue;
			}
			if(moving && (mouse->buttons&7)){
				incref(&winput->ref);
				sweeping = TRUE;
				if(mouse->buttons & 3)
					i = bandsize(winput);
				else
					i = drag(winput);
				sweeping = FALSE;
				if(i != nil)
					wsendctlmesg(winput, Reshaped, i->r, i);
				wclose(winput);
				continue;
			}
			w = wpointto(mouse->xy);
			if(w!=nil && inborder(w->screenr, mouse->xy))
				riosetcursor(corners[whichcorner(w->screenr, mouse->xy)]);
			else
				wsetcursor(w, FALSE);
			/* we're not sending the event, but if button is down maybe we should */
			if(mouse->buttons){
				/* w->topped will be zero or less if window has been bottomed */
				if(w==nil || (w==winput && w->topped>0)){
					if(mouse->buttons & 1){
						;
					}else if(mouse->buttons & 2){
						if(winput && !winput->deleted && !winput->mouseopen){
							incref(&winput->ref);
							button2menu(winput);
							wclose(winput);
						}
					}else if(mouse->buttons & 4)
						button3menu();
				}else{
					/* if button 1 event in the window, top the window and wait for button up. */
					/* otherwise, top the window and pass the event on */
					if(wtop(mouse->xy) && (mouse->buttons!=1 || inborder(w->screenr, mouse->xy)))
						goto Again;
					goto Drain;
				}
			}
			break;

		Drain:
			do
				readmouse(mousectl);
			while(mousectl->buttons);
			goto Again;	/* recalculate mouse position, cursor */
		}
}

int
wtopcmp(const void *a, const void *b)
{
	return (*(Window**)a)->topped - (*(Window**)b)->topped;
}

void
resized(void)
{
	Image *im;
	int i, j;
	Rectangle r;
	Point o, n;
	Window *w;

	if(getwindow(display, Refnone) < 0)
		error("failed to re-attach window");
	freescrtemps();
	view = screen;
	freescreen(wscreen);
	wscreen = allocscreen(screen, background, 0);
	if(wscreen == nil)
		error("can't re-allocate screen");
	draw(view, view->r, background, nil, ZP);
	o = subpt(viewr.max, viewr.min);
	n = subpt(view->clipr.max, view->clipr.min);
	qsort(window, nwindow, sizeof(window[0]), wtopcmp);
	for(i=0; i<nwindow; i++){
		w = window[i];
		r = rectsubpt(w->i->r, viewr.min);
		r.min.x = (r.min.x*n.x)/o.x;
		r.min.y = (r.min.y*n.y)/o.y;
		r.max.x = (r.max.x*n.x)/o.x;
		r.max.y = (r.max.y*n.y)/o.y;
		r = rectaddpt(r, view->clipr.min);
		if(!goodrect(r))
			r = rectsubpt(w->i->r, subpt(w->i->r.min, r.min));
		for(j=0; j<nhidden; j++)
			if(w == hidden[j])
				break;
		incref(&w->ref);
		if(j < nhidden){
			im = allocimage(display, r, screen->chan, 0, DNofill);
			r = ZR;
		} else
			im = allocwindow(wscreen, r, Refbackup, DNofill);
		if(im)
			wsendctlmesg(w, Reshaped, r, im);
		wclose(w);
	}
	viewr = view->r;
	flushimage(display, 1);
}

int
obscured(Window *w, Rectangle r, int i)
{
	Window *t;

	if(Dx(r) < font->height || Dy(r) < font->height)
		return 1;
	if(!rectclip(&r, screen->r))
		return 1;
	for(; i<nwindow; i++){
		t = window[i];
		if(t == w || t->topped <= w->topped)
			continue;
		if(Dx(t->screenr) == 0 || Dy(t->screenr) == 0 || rectXrect(r, t->screenr) == 0)
			continue;
		if(r.min.y < t->screenr.min.y)
			if(!obscured(w, Rect(r.min.x, r.min.y, r.max.x, t->screenr.min.y), i))
				return 0;
		if(r.min.x < t->screenr.min.x)
			if(!obscured(w, Rect(r.min.x, r.min.y, t->screenr.min.x, r.max.y), i))
				return 0;
		if(r.max.y > t->screenr.max.y)
			if(!obscured(w, Rect(r.min.x, t->screenr.max.y, r.max.x, r.max.y), i))
				return 0;
		if(r.max.x > t->screenr.max.x)
			if(!obscured(w, Rect(t->screenr.max.x, r.min.y, r.max.x, r.max.y), i))
				return 0;
		return 1;
	}
	return 0;
}

static char*
shortlabel(char *s)
{
	enum { NBUF=60 };
	static char buf[NBUF*UTFmax];
	int i, k, l;
	Rune r;

	l = utflen(s);
	if(l < NBUF-2)
		return estrdup(s);
	k = i = 0;
	while(i < NBUF/2){
		k += chartorune(&r, s+k);
		i++;
	}
	strncpy(buf, s, k);
	strcpy(buf+k, "...");
	while((l-i) >= NBUF/2-4){
		k += chartorune(&r, s+k);
		i++;
	}
	strcat(buf, s+k);
	return estrdup(buf);
}

void
button3menu(void)
{
	int i, j, n;

	n = nhidden;
	for(i=0; i<nwindow; i++){
		for(j=0; j<n; j++)
			if(window[i] == hidden[j])
				break;
		if(j == n)
			if(obscured(window[i], window[i]->screenr, 0)){
				hidden[n++] = window[i];
				if(n >= nelem(hidden))
					break;
			}
	}
	if(n >= nelem(menu3str)-Hidden)
		n = nelem(menu3str)-Hidden-1;
	for(i=0; i<n; i++){
		free(menu3str[i+Hidden]);
		menu3str[i+Hidden] = shortlabel(hidden[i]->label);
	}
	for(i+=Hidden; menu3str[i]; i++){
		free(menu3str[i]);
		menu3str[i] = nil;
	}
	sweeping = TRUE;
	switch(i = menuhit(3, mousectl, &menu3, wscreen)){
	case -1:
		break;
	case New:
		new(sweep(), FALSE, scrolling, 0, nil, "/cmd/rc", nil);
		break;
	case Reshape:
		resize();
		break;
	case Move:
		move();
		break;
	case Delete:
		delete();
		break;
	case Hide:
		hide();
		break;
	case Exit:
		if(Hidden > Exit){
			send(exitchan, nil);
			break;
		}
		/* else fall through */
	default:
		unhide(i);
		break;
	}
	sweeping = FALSE;
}

void
button2menu(Window *w)
{
	if(w->scrolling)
		menu2str[Scroll] = "noscroll";
	else
		menu2str[Scroll] = "scroll";
	switch(menuhit(2, mousectl, &menu2, wscreen)){
	case Cut:
		wsnarf(w);
		wcut(w);
		wscrdraw(w);
		break;

	case Snarf:
		wsnarf(w);
		break;

	case Paste:
		getsnarf();
		wpaste(w);
		wscrdraw(w);
		break;

	case Plumb:
		wplumb(w);
		break;

	case Look:
		wlook(w);
		break;

	case Send:
		getsnarf();
		wsnarf(w);
		if(nsnarf == 0)
			break;
		if(w->rawing){
			waddraw(w, snarf, nsnarf);
			if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!='\004')
				waddraw(w, (Rune*)L"\n", 1);
		}else{
			winsert(w, snarf, nsnarf, w->nr);
			if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!='\004')
				winsert(w, (Rune*)L"\n", 1, w->nr);
		}
		wsetselect(w, w->nr, w->nr);
		wshow(w, w->nr);
		break;

	case Scroll:
		if(w->scrolling ^= 1)
			wshow(w, w->nr);
		break;
	}
	flushimage(display, 1);
	wsendctlmesg(w, Wakeup, ZR, nil);
}

Point
onscreen(Point p)
{
	p.x = max(screen->clipr.min.x, p.x);
	p.x = min(screen->clipr.max.x, p.x);
	p.y = max(screen->clipr.min.y, p.y);
	p.y = min(screen->clipr.max.y, p.y);
	return p;
}

Image*
sweep(void)
{
	Image *i, *oi;
	Rectangle r;
	Point p0, p;

	i = nil;
	menuing = TRUE;
	riosetcursor(&crosscursor);
	while(mouse->buttons == 0)
		readmouse(mousectl);
	p0 = onscreen(mouse->xy);
	p = p0;
	r.min = p;
	r.max = p;
	oi = nil;
	while(mouse->buttons == 4){
		if(!eqpt(mouse->xy, p)){
			p = onscreen(mouse->xy);
			r = canonrect(Rpt(p0, p));
			r = whichrect(r, p, whichcorner(r, p));
			if(Dx(r)>5 && Dy(r)>5){
				i = allocwindow(wscreen, r, Refnone, DNofill);
				freeimage(oi);
				if(i == nil)
					goto Rescue;
				oi = i;
				border(i, r, Selborder, sizecol, ZP);
				draw(i, insetrect(r, Selborder), cols[BACK], nil, ZP);
			}
		}
		readmouse(mousectl);
	}
	if(mouse->buttons != 0)
		goto Rescue;
	if(i==nil || !goodrect(r))
		goto Rescue;
	oi = i;
	i = allocwindow(wscreen, oi->r, Refbackup, DNofill);
	freeimage(oi);
	if(i == nil)
		goto Rescue;
	riosetcursor(corners[whichcorner(i->r, mouse->xy)]);
	goto Return;

 Rescue:
	riosetcursor(nil);
	freeimage(i);
	i = nil;
	flushimage(display, 1);
	while(mouse->buttons)
		readmouse(mousectl);

 Return:
	menuing = FALSE;
	return i;
}

void
drawedge(Image **bp, Rectangle r)
{
	Image *b = *bp;
	if(b != nil && Dx(b->r) == Dx(r) && Dy(b->r) == Dy(r))
		originwindow(b, r.min, r.min);
	else{
		freeimage(b);
		b = allocwindow(wscreen, r, Refbackup, DNofill);
		if(b != nil) draw(b, r, sizecol, nil, ZP);
		*bp = b;
	}
}

void
drawborder(Rectangle r, int show)
{
	static Image *b[4];
	int i;
	if(show == 0){
		for(i = 0; i < 4; i++){
			freeimage(b[i]);
			b[i] = nil;
		}
	}else{
		r = canonrect(r);
		drawedge(&b[0], Rect(r.min.x, r.min.y, r.min.x+Borderwidth, r.max.y));
		drawedge(&b[1], Rect(r.min.x+Borderwidth, r.min.y, r.max.x-Borderwidth, r.min.y+Borderwidth));
		drawedge(&b[2], Rect(r.max.x-Borderwidth, r.min.y, r.max.x, r.max.y));
		drawedge(&b[3], Rect(r.min.x+Borderwidth, r.max.y-Borderwidth, r.max.x-Borderwidth, r.max.y));
	}
}

Image*
drag(Window *w)
{
	Point p, op, d, dm, om;
	Rectangle r;

	menuing = TRUE;
	riosetcursor(&boxcursor);
	om = mouse->xy;
	dm = subpt(om, w->screenr.min);
	d = subpt(w->screenr.max, w->screenr.min);
	op = subpt(om, dm);
	drawborder(Rect(op.x, op.y, op.x+d.x, op.y+d.y), 1);
	while(mouse->buttons==4){
		p = subpt(mouse->xy, dm);
		if(!eqpt(p, op)){
			drawborder(Rect(p.x, p.y, p.x+d.x, p.y+d.y), 1);
			op = p;
		}
		readmouse(mousectl);
	}
	r = Rect(op.x, op.y, op.x+d.x, op.y+d.y);
	drawborder(r, 0);
	p = mouse->xy;
	riosetcursor(inborder(r, p) ? corners[whichcorner(r, p)] : nil);
	menuing = FALSE;
	if(mouse->buttons!=0 || !goodrect(r) || eqrect(r, w->screenr)){
		flushimage(display, 1);
		while(mouse->buttons)
			readmouse(mousectl);
		return nil;
	}
	return allocwindow(wscreen, r, Refbackup, DNofill);
}

Image*
bandsize(Window *w)
{
	Rectangle r, or;
	Point p, startp;
	int which, but;

	p = mouse->xy;
	but = mouse->buttons;
	which = whichcorner(w->screenr, p);
	riosetcursor(corners[which]);
	r = whichrect(w->screenr, p, which);
	drawborder(r, 1);
	or = r;
	startp = p;

	while(mouse->buttons==but){
		p = onscreen(mouse->xy);
		r = whichrect(w->screenr, p, which);
		if(!eqrect(r, or) && goodrect(r)){
			drawborder(r, 1);
			or = r;
		}
		readmouse(mousectl);
	}
	p = mouse->xy;
	drawborder(or, 0);
	if(mouse->buttons!=0 || !goodrect(or) || eqrect(or, w->screenr)
	|| abs(p.x-startp.x)+abs(p.y-startp.y) <= 1){
		flushimage(display, 1);
		while(mouse->buttons)
			readmouse(mousectl);
		return nil;
	}
	return allocwindow(wscreen, or, Refbackup, DNofill);
}

Window*
pointto(int wait)
{
	Window *w;

	menuing = TRUE;
	riosetcursor(&sightcursor);
	while(mouse->buttons == 0)
		readmouse(mousectl);
	if(mouse->buttons == 4)
		w = wpointto(mouse->xy);
	else
		w = nil;
	if(wait){
		while(mouse->buttons){
			if(mouse->buttons!=4 && w !=nil){	/* cancel */
				riosetcursor(nil);
				w = nil;
			}
			readmouse(mousectl);
		}
		if(w != nil && wpointto(mouse->xy) != w)
			w = nil;
	}
	riosetcursor(nil);
	menuing = FALSE;
	return w;
}

void
delete(void)
{
	Window *w;

	w = pointto(TRUE);
	if(w)
		wsendctlmesg(w, Deleted, ZR, nil);
}

void
resize(void)
{
	Window *w;
	Image *i;

	w = pointto(TRUE);
	if(w == nil)
		return;
	incref(&w->ref);
	i = sweep();
	if(i)
		wsendctlmesg(w, Reshaped, i->r, i);
	wclose(w);
}

void
move(void)
{
	Window *w;
	Image *i;

	w = pointto(FALSE);
	if(w == nil)
		return;
	incref(&w->ref);
	i = drag(w);
	if(i)
		wsendctlmesg(w, Reshaped, i->r, i);
	wclose(w);
}

int
whide(Window *w)
{
	Image *i;
	int j;

	for(j=0; j<nhidden; j++)
		if(hidden[j] == w)	/* already hidden */
			return -1;
	if(nhidden >= nelem(hidden))
		return 0;
	incref(&w->ref);
	i = allocimage(display, w->screenr, w->i->chan, 0, DNofill);
	if(i){
		hidden[nhidden++] = w;
		wsendctlmesg(w, Reshaped, ZR, i);
	}
	wclose(w);
	return i!=0;
}

int
wunhide(Window *w)
{
	int j;
	Image *i;

	for(j=0; j<nhidden; j++)
		if(hidden[j] == w)
			break;
	if(j == nhidden)
		return -1;	/* not hidden */
	incref(&w->ref);
	i = allocwindow(wscreen, w->i->r, Refbackup, DNofill);
	if(i){
		--nhidden;
		memmove(hidden+j, hidden+j+1, (nhidden-j)*sizeof(Window*));
		wsendctlmesg(w, Reshaped, w->i->r, i);
	}
	wclose(w);
	return i!=0;
}

void
hide(void)
{
	Window *w;

	w = pointto(TRUE);
	if(w)
		whide(w);
}

void
unhide(int j)
{
	Window *w;

	if(j < Hidden)
		return;
	j -= Hidden;
	w = hidden[j];
	if(w == nil)
		return;
	if(j < nhidden){
		wunhide(w);
		return;
	}
	/* uncover obscured window */
	for(j=0; j<nwindow; j++)
		if(window[j] == w){
			incref(&w->ref);
			wtopme(w);
			wcurrent(w);
			wclose(w);
			return;
		}
}

Window*
new(Image *i, int hideit, int scrollit, int pid, char *dir, char *cmd, char **argv)
{
	Window *w;
	Mousectl *mc;
	Channel *cm, *ck, *cctl, *cpid;
	void **arg;

	if(i == nil)
		return nil;
	if(hideit && nhidden >= nelem(hidden)){
		freeimage(i);
		return nil;
	}
	cm = chancreate(sizeof(Mouse), 0);
	ck = chancreate(sizeof(char*), 0);
	cctl = chancreate(sizeof(Wctlmesg), 4);
	cpid = chancreate(sizeof(int), 0);
	if(cm==nil || ck==nil || cctl==nil)
		error("new: channel alloc failed");
	mc = emalloc(sizeof(Mousectl));
	*mc = *mousectl;
	mc->image = i;
	mc->c = cm;
	w = wmk(i, mc, ck, cctl, scrollit);
	free(mc);	/* wmk copies *mc */
	window = erealloc(window, ++nwindow*sizeof(Window*));
	window[nwindow-1] = w;
	if(hideit){
		hidden[nhidden++] = w;
		w->screenr = ZR;
	}
	threadcreate(winctl, w, 8192);
	if(!hideit)
		wcurrent(w);
	if(pid == 0){
		arg = emalloc(5*sizeof(void*));
		arg[0] = w;
		arg[1] = cpid;
		arg[2] = cmd;
		if(argv == nil)
			arg[3] = rcargv;
		else
			arg[3] = argv;
		arg[4] = dir;
		proccreate(winshell, arg, 8192);
		pid = recvul(cpid);
		free(arg);
	}
	if(pid == 0){
		/* window creation failed */
		wsendctlmesg(w, Deleted, ZR, nil);
		chanfree(cpid);
		return nil;
	}
	wsetpid(w, pid, 1);
	wsetname(w);
	if(dir){
		free(w->dir);
		w->dir = estrdup(dir);
	}
	chanfree(cpid);
	return w;
}

static void
kbdproc(void *arg)
{
	Channel *c = arg;
	char buf[128], *p, *e;
	int fd, cfd, kfd, n;

	threadsetname("kbdproc");

	if((fd = sys_open("/dev/cons", OREAD)) < 0){
		chanprint(c, "%r");
		return;
	}
	if((cfd = sys_open("/dev/consctl", OWRITE)) < 0){
		chanprint(c, "%r");
		return;
	}
	fprint(cfd, "rawon");

	if(sendp(c, nil) <= 0)
		return;

	if((kfd = sys_open("/dev/kbd", OREAD)) >= 0){
		sys_close(fd);

		/* only serve a kbd file per window when we got one */
		servekbd = 1;

		/* read kbd state */
		while((n = jehanne_read(kfd, buf, sizeof(buf)-1)) > 0){
			e = buf+n;
			e[-1] = 0;
			e[0] = 0;
			for(p = buf; p < e; p += strlen(p)+1)
				chanprint(c, "%s", p);
		}
	} else {
		/* read single characters */
		p = buf;
		for(;;){
			Rune r;

			e = buf + sizeof(buf);
			if((n = jehanne_read(fd, p, e-p)) <= 0)
				break;
			e = p + n;
			while(p < e && fullrune(p, e - p)){
				p += chartorune(&r, p);
				if(r)
					chanprint(c, "c%C", r);
			}
			n = e - p;
			memmove(buf, p, n);
			p = buf + n;
		}
	}
	send(exitchan, nil);
}

Channel*
initkbd(void)
{
	Channel *c;
	char *e;

	c = chancreate(sizeof(char*), 16);
	procrfork(kbdproc, c, STACK, RFCFDG);
	if(e = recvp(c)){
		chanfree(c);
		c = nil;
		werrstr("%s", e);
		free(e);
	}
	return c;
}
