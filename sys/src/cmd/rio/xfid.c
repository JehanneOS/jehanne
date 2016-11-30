#include <u.h>
#include <libc.h>
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

char Einuse[] =		"file in use";
char Edeleted[] =	"window deleted";
char Etooshort[] =	"buffer too small";
char Eshort[] =		"short i/o request";
char Elong[] = 		"snarf buffer too int32_t";
char Eunkid[] = 	"unknown id in attach";
char Ebadrect[] = 	"bad rectangle in attach";
char Ewindow[] = 	"cannot make window";
char Enowindow[] = 	"window has no image";
char Ebadmouse[] = 	"bad format on /dev/mouse";

extern char Eperm[];
extern char Enomem[];

static	Xfid	*xfidfree;
static	Xfid	*xfid;
static	Channel	*cxfidalloc;	/* chan(Xfid*) */
static	Channel	*cxfidfree;	/* chan(Xfid*) */

static	char	*tsnarf;
static	int	ntsnarf;

void
xfidallocthread(void* _)
{
	Xfid *x;
	enum { Alloc, Free, N };
	static Alt alts[N+1];

	alts[Alloc].c = cxfidalloc;
	alts[Alloc].v = nil;
	alts[Alloc].op = CHANRCV;
	alts[Free].c = cxfidfree;
	alts[Free].v = &x;
	alts[Free].op = CHANRCV;
	alts[N].op = CHANEND;
	for(;;){
		switch(alt(alts)){
		case Alloc:
			x = xfidfree;
			if(x)
				xfidfree = x->free;
			else{
				x = emalloc(sizeof(Xfid));
				x->c = chancreate(sizeof(void(*)(Xfid*)), 0);
				x->flushc = chancreate(sizeof(int), 0);	/* notification only; no data */
				x->flushtag = -1;
				x->next = xfid;
				xfid = x;
				threadcreate(xfidctl, x, 16384);
			}
			if(x->ref.ref != 0){
				fprint(2, "%p incref %ld\n", x, x->ref);
				error("incref");
			}
			if(x->flushtag != -1)
				error("flushtag in allocate");
			incref(&x->ref);
			sendp(cxfidalloc, x);
			break;
		case Free:
			if(x->ref.ref != 0){
				fprint(2, "%p decref %ld\n", x, x->ref);
				error("decref");
			}
			if(x->flushtag != -1)
				error("flushtag in free");
			x->free = xfidfree;
			xfidfree = x;
			break;
		}
	}
}

Channel*
xfidinit(void)
{
	cxfidalloc = chancreate(sizeof(Xfid*), 0);
	cxfidfree = chancreate(sizeof(Xfid*), 0);
	threadcreate(xfidallocthread, nil, STACK);
	return cxfidalloc;
}

void
xfidctl(void *arg)
{
	Xfid *x;
	void (*f)(Xfid*);

	x = arg;
	threadsetname("xfid.%p", x);
	for(;;){
		f = recvp(x->c);
		if(f){
			x->flushtag = x->tag;
			(*f)(x);
		}
		if(decref(&x->ref) == 0)
			sendp(cxfidfree, x);
	}
}

void
xfidflush(Xfid *x)
{
	Fcall t;
	Xfid *xf;

	for(xf=xfid; xf; xf=xf->next)
		if(xf->flushtag == x->oldtag){
			incref(&xf->ref);	/* to hold data structures up at tail of synchronization */
			if(xf->ref.ref == 1)
				error("ref 1 in flush");
			xf->flushtag = -1;
			break;
		}

	/* take over flushtag so follow up flushes wait for us */
	x->flushtag = x->oldtag;

	/*
	 * wakeup filsysflush() in the filsysproc so the next
	 * flush can come in.
	 */
	sendul(x->fs->csyncflush, 0);

	if(xf){
		enum { Done, Flush, End };
		Alt alts[End+1];
		void *f;
		int z;

		z = 0;
		f = nil;

		alts[Done].c = xf->c;
		alts[Done].v = &f;
		alts[Done].op = CHANSND;
		alts[Flush].c = xf->flushc;
		alts[Flush].v = &z;
		alts[Flush].op = CHANSND;
		alts[End].op = CHANEND;

		while(alt(alts) != Done)
			;
	}
	if(nbrecv(x->flushc, nil)){
		filsyscancel(x);
		return;
	}
	filsysrespond(x->fs, x, &t, nil);
}

void
xfidattach(Xfid *x)
{
	Fcall t;
	int id, hideit, scrollit;
	Window *w;
	char *err, *n, *dir, errbuf[ERRMAX];
	int pid, newlymade;
	Rectangle r;
	Image *i;

	t.qid = x->f->qid;
	qlock(&all);
	w = nil;
	err = Eunkid;
	dir = nil;
	newlymade = FALSE;
	hideit = 0;
	scrollit = scrolling;

	if(x->aname[0] == 'N'){	/* N 100,100, 200, 200 - old syntax */
		n = x->aname+1;
		pid = strtoul(n, &n, 0);
		if(*n == ',')
			n++;
		r.min.x = strtoul(n, &n, 0);
		if(*n == ',')
			n++;
		r.min.y = strtoul(n, &n, 0);
		if(*n == ',')
			n++;
		r.max.x = strtoul(n, &n, 0);
		if(*n == ',')
			n++;
		r.max.y = strtoul(n, &n, 0);
  Allocate:
		if(!goodrect(r))
			err = Ebadrect;
		else{
			if(hideit)
				i = allocimage(display, r, screen->chan, 0, DNofill);
			else
				i = allocwindow(wscreen, r, Refbackup, DNofill);
			if(i){
				if(pid == 0)
					pid = -1;	/* make sure we don't pop a shell! - UGH */
				w = new(i, hideit, scrollit, pid, dir, nil, nil);
				newlymade = TRUE;
			}else
				err = Ewindow;
		}
	}else if(strncmp(x->aname, "new", 3) == 0){	/* new -dx -dy - new syntax, as in wctl */
		pid = 0;
		if(parsewctl(nil, ZR, &r, &pid, nil, &hideit, &scrollit, &dir, x->aname, errbuf) < 0)
			err = errbuf;
		else
			goto Allocate;
	}else{
		id = atoi(x->aname);
		w = wlookid(id);
	}
	x->f->w = w;
	if(w == nil){
		qunlock(&all);
		x->f->busy = FALSE;
		filsysrespond(x->fs, x, &t, err);
		return;
	}
	if(!newlymade)	/* counteract dec() in winshell() */
		incref(&w->ref);
	qunlock(&all);
	filsysrespond(x->fs, x, &t, nil);
}

void
xfidopen(Xfid *x)
{
	Fcall t;
	Window *w;

	w = x->f->w;
	if(w->deleted){
		filsysrespond(x->fs, x, &t, Edeleted);
		return;
	}
	switch(FILE(x->f->qid)){
	case Qconsctl:
		if(w->ctlopen){
			filsysrespond(x->fs, x, &t, Einuse);
			return;
		}
		w->ctlopen = TRUE;
		break;
	case Qkbd:
		if(w->kbdopen){
			filsysrespond(x->fs, x, &t, Einuse);
			return;
		}
		w->kbdopen = TRUE;
		break;
	case Qmouse:
		if(w->mouseopen){
			filsysrespond(x->fs, x, &t, Einuse);
			return;
		}
		/*
		 * Reshaped: there's a race if the appl. opens the
		 * window, is resized, and then opens the mouse,
		 * but that's rare.  The alternative is to generate
		 * a resized event every time a new program starts
		 * up in a window that has been resized since the
		 * dawn of time.  We choose the lesser evil.
		 */
		w->resized = FALSE;
		w->mouseopen = TRUE;
		break;
	case Qsnarf:
		if(x->mode==NP_ORDWR || x->mode==NP_OWRITE)
			ntsnarf = 0;
		break;
	case Qwctl:
		if(x->mode==NP_OREAD || x->mode==NP_ORDWR){
			/*
			 * It would be much nicer to implement fan-out for wctl reads,
			 * so multiple people can see the resizings, but rio just isn't
			 * structured for that.  It's structured for /dev/cons, which gives
			 * alternate data to alternate readers.  So to keep things sane for
			 * wctl, we compromise and give an error if two people try to
			 * open it.  Apologies.
			 */
			if(w->wctlopen){
				filsysrespond(x->fs, x, &t, Einuse);
				return;
			}
			w->wctlopen = TRUE;
			w->wctlready = 1;
			wsendctlmesg(w, Wakeup, ZR, nil);
		}
		break;
	}
	t.qid = x->f->qid;
	t.iounit = messagesize-IOHDRSZ;
	x->f->open = TRUE;
	x->f->mode = x->mode;
	filsysrespond(x->fs, x, &t, nil);
}

void
xfidclose(Xfid *x)
{
	Fcall t;
	Window *w;
	int nb, nulls;

	w = x->f->w;
	switch(FILE(x->f->qid)){
	case Qconsctl:
		if(w->rawing){
			w->rawing = FALSE;
			wsendctlmesg(w, Rawoff, ZR, nil);
		}
		if(w->holding){
			w->holding = FALSE;
			wsendctlmesg(w, Holdoff, ZR, nil);
		}
		w->ctlopen = FALSE;
		break;
	case Qcursor:
		w->cursorp = nil;
		wsetcursor(w, FALSE);
		break;
	case Qkbd:
		w->kbdopen = FALSE;
		break;
	case Qmouse:
		w->resized = FALSE;
		w->mouseopen = FALSE;
		if(w->i != nil)
			wsendctlmesg(w, Refresh, w->i->r, nil);
		break;
	/* odd behavior but really ok: replace snarf buffer when /dev/snarf is closed */
	case Qsnarf:
		if(x->f->mode==NP_ORDWR || x->f->mode==NP_OWRITE){
			snarf = runerealloc(snarf, ntsnarf+1);
			cvttorunes(tsnarf, ntsnarf, snarf, &nb, &nsnarf, &nulls);
			ntsnarf = 0;
		}
		break;
	case Qwctl:
		if(x->f->mode==NP_OREAD || x->f->mode==NP_ORDWR)
			w->wctlopen = FALSE;
		break;
	}
	wclose(w);
	filsysrespond(x->fs, x, &t, nil);
}

void
xfidwrite(Xfid *x)
{
	Fcall fc;
	int cnt, qid, nb, off, nr;
	char err[ERRMAX], *p;
	Point pt;
	Window *w;
	Rune *r;
	Conswritemesg cwm;
	Stringpair pair;
	enum { CWdata, CWgone, CWflush, NCW };
	Alt alts[NCW+1];

	w = x->f->w;
	if(w->deleted){
		filsysrespond(x->fs, x, &fc, Edeleted);
		return;
	}
	qid = FILE(x->f->qid);
	cnt = x->count;
	off = x->offset;
	x->data[cnt] = 0;
	switch(qid){
	case Qcons:
		alts[CWdata].c = w->conswrite;
		alts[CWdata].v = &cwm;
		alts[CWdata].op = CHANRCV;
		alts[CWgone].c = w->gone;
		alts[CWgone].v = nil;
		alts[CWgone].op = CHANRCV;
		alts[CWflush].c = x->flushc;
		alts[CWflush].v = nil;
		alts[CWflush].op = CHANRCV;
		alts[NCW].op = CHANEND;

		switch(alt(alts)){
		case CWdata:
			break;
		case CWgone:
			filsysrespond(x->fs, x, &fc, Edeleted);
			return;
		case CWflush:
			filsyscancel(x);
			return;
		}

		nr = x->f->nrpart;
		if(nr > 0){
			memmove(x->data+nr, x->data, cnt);	/* there's room: see malloc in filsysproc */
			memmove(x->data, x->f->rpart, nr);
			cnt += nr;
		}
		r = runemalloc(cnt);
		if(r == nil){
			pair.ns = 0;
			send(cwm.cw, &pair);
			filsysrespond(x->fs, x, &fc, Enomem);
			return;
		}
		x->f->nrpart = 0;
		cvttorunes(x->data, cnt-UTFmax, r, &nb, &nr, nil);
		/* approach end of buffer */
		while(fullrune(x->data+nb, cnt-nb)){
			nb += chartorune(&r[nr], x->data+nb);
			if(r[nr])
				nr++;
		}
		if(nb < cnt){
			memmove(x->f->rpart, x->data+nb, cnt-nb);
			x->f->nrpart = cnt-nb;
		}

		pair.s = r;
		pair.ns = nr;
		send(cwm.cw, &pair);
		fc.count = x->count;
		filsysrespond(x->fs, x, &fc, nil);
		return;

	case Qconsctl:
		if(strncmp(x->data, "holdon", 6)==0){
			if(w->holding++ == 0)
				wsendctlmesg(w, Holdon, ZR, nil);
			break;
		}
		if(strncmp(x->data, "holdoff", 7)==0 && w->holding){
			if(--w->holding == 0)
				wsendctlmesg(w, Holdoff, ZR, nil);
			break;
		}
		if(strncmp(x->data, "rawon", 5)==0){
			if(w->holding){
				w->holding = 0;
				wsendctlmesg(w, Holdoff, ZR, nil);
			}
			if(w->rawing++ == 0)
				wsendctlmesg(w, Rawon, ZR, nil);
			break;
		}
		if(strncmp(x->data, "rawoff", 6)==0 && w->rawing){
			if(--w->rawing == 0)
				wsendctlmesg(w, Rawoff, ZR, nil);
			break;
		}
		filsysrespond(x->fs, x, &fc, "unknown control message");
		return;

	case Qcursor:
		if(cnt < 2*4+2*2*16)
			w->cursorp = nil;
		else{
			w->cursor.offset.x = BGLONG(x->data+0*4);
			w->cursor.offset.y = BGLONG(x->data+1*4);
			memmove(w->cursor.clr, x->data+2*4, 2*2*16);
			w->cursorp = &w->cursor;
		}
		wsetcursor(w, TRUE);
		break;

	case Qlabel:
		if(off != 0){
			filsysrespond(x->fs, x, &fc, "non-zero offset writing label");
			return;
		}
		p = realloc(w->label, cnt+1);
		if(p == nil){
			filsysrespond(x->fs, x, &fc, Enomem);
			return;
		}
		w->label = p;
		w->label[cnt] = 0;
		memmove(w->label, x->data, cnt);
		break;

	case Qmouse:
		if(w!=input || Dx(w->screenr)<=0)
			break;
		if(x->data[0] != 'm'){
			filsysrespond(x->fs, x, &fc, Ebadmouse);
			return;
		}
		p = nil;
		pt.x = strtoul(x->data+1, &p, 0);
		if(p == nil){
			filsysrespond(x->fs, x, &fc, Eshort);
			return;
		}
		pt.y = strtoul(p, nil, 0);
		if(w==input && wpointto(mouse->xy)==w)
			wsendctlmesg(w, Movemouse, Rpt(pt, pt), nil);
		break;

	case Qsnarf:
		if(cnt == 0)
			break;
		/* always append only */
		if(ntsnarf > MAXSNARF){	/* avoid thrashing when people cut huge text */
			filsysrespond(x->fs, x, &fc, Elong);
			return;
		}
		p = realloc(tsnarf, ntsnarf+cnt+1);	/* room for NUL */
		if(p == nil){
			filsysrespond(x->fs, x, &fc, Enomem);
			return;
		}
		tsnarf = p;
		memmove(tsnarf+ntsnarf, x->data, cnt);
		ntsnarf += cnt;
		snarfversion++;
		break;

	case Qwdir:
		if(cnt == 0)
			break;
		if(x->data[cnt-1] == '\n'){
			if(cnt == 1)
				break;
			x->data[cnt-1] = '\0';
		}
		/* assume data comes in a single write */
		if(x->data[0] == '/'){
			p = smprint("%.*s", cnt, x->data);
		}else{
			p = smprint("%s/%.*s", w->dir, cnt, x->data);
		}
		if(p == nil){
			filsysrespond(x->fs, x, &fc, Enomem);
			return;
		}
		free(w->dir);
		w->dir = cleanname(p);
		break;

	case Qwctl:
		if(writewctl(x, err) < 0){
			filsysrespond(x->fs, x, &fc, err);
			return;
		}
		break;

	default:
		fprint(2, "unknown qid %d in write\n", qid);
		filsysrespond(x->fs, x, &fc, "unknown qid in write");
		return;
	}
	fc.count = cnt;
	filsysrespond(x->fs, x, &fc, nil);
}

int
readwindow(Image *i, char *t, Rectangle r, int offset, int n)
{
	int ww, oo, y, m;
	uint8_t *tt;

	ww = bytesperline(r, i->depth);
	r.min.y += offset/ww;
	if(r.min.y >= r.max.y)
		return 0;
	y = r.min.y + (n + ww-1)/ww;
	if(y < r.max.y)
		r.max.y = y;
	m = ww * Dy(r);
	oo = offset % ww;
	if(oo == 0 && n >= m)
		return unloadimage(i, r, (uint8_t*)t, n);
	if((tt = malloc(m)) == nil)
		return -1;
	m = unloadimage(i, r, tt, m) - oo;
	if(m > 0){
		if(n < m) m = n;
		memmove(t, tt + oo, m);
	}
	free(tt);
	return m;
}

void
xfidread(Xfid *x)
{
	Fcall fc;
	int n, off, cnt, c;
	uint32_t qid;
	char buf[128], *t;
	char cbuf[30];
	Window *w;
	Mouse ms;
	Rectangle r;
	Image *i;
	Channel *c1, *c2;	/* chan (tuple(char*, int)) */
	Consreadmesg crm;
	Mousereadmesg mrm;
	Stringpair pair;
	enum { Adata, Agone, Aflush, Aend };
	Alt alts[Aend+1];

	w = x->f->w;
	if(w->deleted){
		filsysrespond(x->fs, x, &fc, Edeleted);
		return;
	}
	qid = FILE(x->f->qid);
	off = x->offset;
	cnt = x->count;
	switch(qid){
	case Qwctl:
		if(cnt < 4*12){
			filsysrespond(x->fs, x, &fc, Etooshort);
			return;
		}
		alts[Adata].c = w->wctlread;
		goto Consmesg;

	case Qkbd:
		alts[Adata].c = w->kbdread;
		goto Consmesg;

	case Qcons:
		alts[Adata].c = w->consread;

	Consmesg:
		alts[Adata].v = &crm;
		alts[Adata].op = CHANRCV;
		alts[Agone].c = w->gone;
		alts[Agone].v = nil;
		alts[Agone].op = CHANRCV;
		alts[Aflush].c = x->flushc;
		alts[Aflush].v = nil;
		alts[Aflush].op = CHANRCV;
		alts[Aend].op = CHANEND;

		switch(alt(alts)){
		case Adata:
			break;
		case Agone:
			filsysrespond(x->fs, x, &fc, Edeleted);
			return;
		case Aflush:
			filsyscancel(x);
			return;
		}
		c1 = crm.c1;
		c2 = crm.c2;
		t = emalloc(cnt+UTFmax+1);	/* room to unpack partial rune plus */
		pair.s = t;
		pair.ns = cnt;
		send(c1, &pair);
		recv(c2, &pair);
		fc.data = pair.s;
		fc.count = min(cnt, pair.ns);
		filsysrespond(x->fs, x, &fc, nil);
		free(t);
		break;

	case Qlabel:
		n = strlen(w->label);
		if(off > n)
			off = n;
		if(off+cnt > n)
			cnt = n-off;
		fc.data = w->label+off;
		fc.count = cnt;
		filsysrespond(x->fs, x, &fc, nil);
		break;

	case Qmouse:
		alts[Adata].c = w->mouseread;
		alts[Adata].v = &mrm;
		alts[Adata].op = CHANRCV;
		alts[Agone].c = w->gone;
		alts[Agone].v = nil;
		alts[Agone].op = CHANRCV;
		alts[Aflush].c = x->flushc;
		alts[Aflush].v = nil;
		alts[Aflush].op = CHANRCV;
		alts[Aend].op = CHANEND;

		switch(alt(alts)){
		case Adata:
			break;
		case Agone:
			filsysrespond(x->fs, x, &fc, Edeleted);
			return;
		case Aflush:
			filsyscancel(x);
			return;
		}

		recv(mrm.cm, &ms);
		c = 'm';
		if(w->resized)
			c = 'r';
		n = sprint(buf, "%c%11d %11d %11d %11ld ", c, ms.xy.x, ms.xy.y, ms.buttons, ms.msec);
		w->resized = 0;
		fc.data = buf;
		fc.count = min(n, cnt);
		filsysrespond(x->fs, x, &fc, nil);
		break;

	case Qcursor:
		filsysrespond(x->fs, x, &fc, "cursor read not implemented");
		break;

	/* The algorithm for snarf and text is expensive but easy and rarely used */
	case Qsnarf:
		getsnarf();
		if(nsnarf)
			t = runetobyte(snarf, nsnarf, &n);
		else {
			t = nil;
			n = 0;
		}
		goto Text;

	case Qtext:
		t = wcontents(w, &n);
		goto Text;

	Text:
		if(off > n){
			off = n;
			cnt = 0;
		}
		if(off+cnt > n)
			cnt = n-off;
		fc.data = t+off;
		fc.count = cnt;
		filsysrespond(x->fs, x, &fc, nil);
		free(t);
		break;

	case Qwdir:
		t = estrdup(w->dir);
		n = strlen(t);
		goto Text;

	case Qwinid:
		n = sprint(buf, "%11d ", w->id);
		t = estrdup(buf);
		goto Text;


	case Qwinname:
		n = strlen(w->name);
		if(n == 0){
			filsysrespond(x->fs, x, &fc, "window has no name");
			break;
		}
		t = estrdup(w->name);
		goto Text;

	case Qwindow:
		i = w->i;
		if(i == nil){
			filsysrespond(x->fs, x, &fc, Enowindow);
			return;
		}
		r = i->r;
		goto caseImage;

	case Qscreen:
		i = screen;
		r = screen->r;

	caseImage:
		if(off < 5*12){
			n = sprint(buf, "%11s %11d %11d %11d %11d ",
				chantostr(cbuf, i->chan),
				r.min.x, r.min.y, r.max.x, r.max.y);
			t = estrdup(buf);
			goto Text;
		}
		off -= 5*12;
		n = -1;
		t = malloc(cnt);
		if(t){
			fc.data = t;
			n = readwindow(i, t, r, off, cnt);	/* careful; fc.count is unsigned */
		}
		if(n < 0){
			buf[0] = 0;
			errstr(buf, sizeof buf);
			filsysrespond(x->fs, x, &fc, buf);
		}else{
			fc.count = n;
			filsysrespond(x->fs, x, &fc, nil);
		}
		free(t);
		return;

	default:
		fprint(2, "unknown qid %d in read\n", qid);
		snprint(buf, sizeof(buf), "unknown qid in read");
		filsysrespond(x->fs, x, &fc, buf);
		break;
	}
}
