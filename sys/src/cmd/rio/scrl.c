#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

static Image *scrtmp;

static
Image*
scrtemps(void)
{
	int h;

	if(scrtmp == nil){
		h = BIG*Dy(screen->r);
		scrtmp = allocimage(display, Rect(0, 0, 32, h), screen->chan, 0, DNofill);
	}
	return scrtmp;
}

void
freescrtemps(void)
{
	if(scrtmp){
		freeimage(scrtmp);
		scrtmp = nil;
	}
}

static
Rectangle
scrpos(Rectangle r, uint32_t p0, uint32_t p1, uint32_t tot)
{
	Rectangle q;
	int h;

	q = r;
	h = q.max.y-q.min.y;
	if(tot == 0)
		return q;
	if(tot > 1024*1024){
		tot>>=10;
		p0>>=10;
		p1>>=10;
	}
	if(p0 > 0)
		q.min.y += h*p0/tot;
	if(p1 < tot)
		q.max.y -= h*(tot-p1)/tot;
	if(q.max.y < q.min.y+2){
		if(q.min.y+2 <= r.max.y)
			q.max.y = q.min.y+2;
		else
			q.min.y = q.max.y-2;
	}
	return q;
}

void
wscrdraw(Window *w)
{
	Rectangle r, r1, r2;
	Image *b;

	b = scrtemps();
	if(b == nil || w->i == nil)
		return;
	r = w->scrollr;
	r1 = r;
	r1.min.x = 0;
	r1.max.x = Dx(r);
	r2 = scrpos(r1, w->org, w->org+w->frame.nchars, w->nr);
	if(!eqrect(r2, w->lastsr)){
		w->lastsr = r2;
		/* move r1, r2 to (0,0) to avoid clipping */
		r2 = rectsubpt(r2, r1.min);
		r1 = rectsubpt(r1, r1.min);
		draw(b, r1, w->frame.cols[BORD], nil, ZP);
		draw(b, r2, w->frame.cols[BACK], nil, ZP);
		r2.min.x = r2.max.x-1;
		draw(b, r2, w->frame.cols[BORD], nil, ZP);
		draw(w->i, r, b, nil, Pt(0, r1.min.y));
	}
}

void
wscrsleep(Window *w, uint32_t dt)
{
	Timer	*timer;
	int y, b;
	static Alt alts[3];

	if(display->bufp > display->buf)
		flushimage(display, 1);
	timer = timerstart(dt);
	y = w->mc.xy.y;
	b = w->mc.buttons;
	alts[0].c = timer->c;
	alts[0].v = nil;
	alts[0].op = CHANRCV;
	alts[1].c = w->mc.c;
	alts[1].v = &w->mc.Mouse;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;
	for(;;)
		switch(alt(alts)){
		case 0:
			timerstop(timer);
			return;
		case 1:
			if(abs(w->mc.xy.y-y)>2 || w->mc.buttons!=b){
				timercancel(timer);
				return;
			}
			break;
		}
}

void
wscroll(Window *w, int but)
{
	uint32_t p0, oldp0;
	Rectangle s;
	int y, my, h, first;

	s = insetrect(w->scrollr, 1);
	h = s.max.y-s.min.y;
	oldp0 = ~0;
	first = TRUE;
	do{
		my = w->mc.xy.y;
		if(my < s.min.y)
			my = s.min.y;
		if(my >= s.max.y)
			my = s.max.y;
		if(but == 2){
			y = my;
			if(y > s.max.y-2)
				y = s.max.y-2;
			if(w->nr > 1024*1024)
				p0 = ((w->nr>>10)*(y-s.min.y)/h)<<10;
			else
				p0 = w->nr*(y-s.min.y)/h;
			if(oldp0 != p0)
				wsetorigin(w, p0, FALSE);
			oldp0 = p0;
			readmouse(&w->mc);
			continue;
		}
		if(but == 1 || but == 4){
			y = max(1, (my-s.min.y)/w->frame.font->height);
			p0 = wbacknl(w, w->org, y);
		}else{
			y = max(my, s.min.y+w->frame.font->height);
			p0 = w->org+frcharofpt(&w->frame, Pt(s.max.x, y));
		}
		if(oldp0 != p0)
			wsetorigin(w, p0, TRUE);
		oldp0 = p0;
		/* debounce */
		if(first){
			if(display->bufp > display->buf)
				flushimage(display, 1);
			if(but > 3)
				return;
			sleep(200);
			nbrecv(w->mc.c, &w->mc.Mouse);
			first = FALSE;
		}
		wscrsleep(w, 100);
	}while(w->mc.buttons & (1<<(but-1)));
	while(w->mc.buttons)
		readmouse(&w->mc);
}
