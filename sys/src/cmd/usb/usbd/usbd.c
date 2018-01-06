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
#include <thread.h>
#include <9P2000.h>
#include <9p.h>
#include "usb.h"
#include "dat.h"
#include "fns.h"

enum {
	Qroot,
	Qusbevent,
	Qmax
};

char *names[] = {
	"",
	"usbevent",
};

static char Enonexist[] = "does not exist";

typedef struct Event Event;

struct Event {
	Dev *dev;	/* the device producing the event,
			   dev->aux points to Fid processing the event */
	char *data;
	int len;
	Event *link;
	int ref;	/* number of readers which will read this one
			   the next time they'll read */
	int prev;	/* number of events pointing to this one with
			   their link pointers */
};

static Event *evlast;
static Req *reqfirst, *reqlast;
static QLock evlock;

static void
addreader(Req *req)
{
	req->aux = nil;
	if(reqfirst == nil)
		reqfirst = req;
	else
		reqlast->aux = req;
	reqlast = req;
}

static void
fulfill(Req *req, Event *e)
{
	int n;

	n = e->len;
	if(n > req->ifcall.count)
		n = req->ifcall.count;
	memmove(req->ofcall.data, e->data, n);
	req->ofcall.count = n;
}

static void
initevent(void)
{
	evlast = emallocz(sizeof(Event), 1);
}

static Event*
putevent(Event *e)
{
	Event *ee;

	ee = e->link;
	if(e->ref || e->prev)
		return ee;
	ee->prev--;
	closedev(e->dev);
	free(e->data);
	free(e);
	return ee;
}

static void
procreqs(void)
{
	Req *r, *p, *x;
	Event *e;
	Fid *f;

Loop:
	for(p = nil, r = reqfirst; r != nil; p = r, r = x){
		x = r->aux;
		f = r->fid;
		e = f->aux;
		if(e == evlast)
			continue;
		if(e->dev->aux == f){
			e->dev->aux = nil;	/* release device */
			e->ref--;
			e = putevent(e);
			e->ref++;
			f->aux = e;
			goto Loop;
		}
		if(e->dev->aux == nil){
			e->dev->aux = f;	/* claim device */
			if(x == nil)
				reqlast = p;
			if(p == nil)
				reqfirst = x;
			else
				p->aux = x;
			r->aux = nil;
			fulfill(r, e);
			respond(r, nil);
			goto Loop;
		}
	}
}

static void
pushevent(Dev *d, char *data)
{
	Event *e;

	qlock(&evlock);
	e = evlast;
	evlast = emallocz(sizeof(Event), 1);
	incref(&d->ref);
	e->dev = d;
	e->data = data;
	e->len = strlen(data);
	e->link = evlast;
	evlast->prev++;
	procreqs();
	putevent(e);
	qunlock(&evlock);
}

static int
dirgen(int n, Dir *d, void * _)
{
	if(n >= Qmax - 1)
		return -1;
	d->qid.path = n + 1;
	d->qid.vers = 0;
	if(n >= 0){
		d->qid.type = 0;
		d->mode = 0444;
	}else{
		d->qid.type = QTDIR;
		d->mode = 0555 | DMDIR;
	}
	d->uid = estrdup9p(getuser());
	d->gid = estrdup9p(d->uid);
	d->muid = estrdup9p(d->uid);
	d->name = estrdup9p(names[n+1]);
	d->atime = d->mtime = time(0);
	d->length = 0;
	return 0;
}

static void
usbdattach(Req *req)
{
	req->fid->qid = (Qid) {Qroot, 0, QTDIR};
	req->ofcall.qid = req->fid->qid;
	respond(req, nil);
}

static char *
usbdwalk(Fid *fid, char *name, Qid *qid)
{
	int i;

	if(fid->qid.path != Qroot)
		return "not a directory";
	if(strcmp(name, "..") == 0){
		*qid = fid->qid;
		return nil;
	}
	for(i = Qroot+1; i < Qmax; i++)
		if(strcmp(name, names[i]) == 0){
			fid->qid = (Qid) {i, 0, 0};
			*qid = fid->qid;
			return nil;
		}
	return Enonexist;
}

static void
usbdread(Req *req)
{
	switch((int32_t)req->fid->qid.path){
	case Qroot:
		dirread9p(req, dirgen, nil);
		respond(req, nil);
		break;
	case Qusbevent:
		qlock(&evlock);
		addreader(req);
		procreqs();
		qunlock(&evlock);
		break;
	default:
		respond(req, Enonexist);
		break;
	}
}

static void
usbdstat(Req *req)
{
	if(dirgen(req->fid->qid.path - 1, &req->d, nil) < 0)
		respond(req, Enonexist);
	else
		respond(req, nil);
}

static char *
formatdev(Dev *d, int type)
{
	Usbdev *u = d->usb;
	return smprint("%s %d %.4x %.4x %.6lx %s\n",
		type ? "detach" : "attach",
		d->id, u->vid, u->did, u->csp,
		d->hname != nil ? d->hname : "");
}

static void
enumerate(Event **l)
{
	extern Hub *hubs;

	Event *e;
	Hub *h;
	Port *p;
	Dev *d;
	int i;

	for(h = hubs; h != nil; h = h->next){
		for(i = 1; i <= h->nport; i++){
			p = &h->port[i];
			d = p->dev;
			if(d == nil || d->usb == nil || p->hub != nil)
				continue;
			e = emallocz(sizeof(Event), 1);
			incref(&d->ref);
			e->dev = d;
			e->data = formatdev(d, 0);
			e->len = strlen(e->data);
			e->prev = 1;
			*l = e;
			l = &e->link;

		}
	}
	*l = evlast;
	evlast->prev++;
}

static void
usbdopen(Req *req)
{
	extern QLock hublock;

	if(req->fid->qid.path == Qusbevent){
		Event *e;

		qlock(&hublock);
		qlock(&evlock);

		enumerate(&e);
		e->prev--;
		e->ref++;
		req->fid->aux = e;

		qunlock(&evlock);
		qunlock(&hublock);
	}
	respond(req, nil);
}

static void
usbddestroyfid(Fid *fid)
{
	if(fid->qid.path == Qusbevent){
		Event *e;

		qlock(&evlock);
		e = fid->aux;
		if(e != nil){
			fid->aux = nil;
			if(e->dev != nil && e->dev->aux == fid){
				e->dev->aux = nil;	/* release device */
				procreqs();
			}
			e->ref--;
			while(e->ref == 0 && e->prev == 0 && e != evlast)
				e = putevent(e);
		}
		qunlock(&evlock);
	}
}

static void
usbdflush(Req *req)
{
	Req *r, *p, *x;

	qlock(&evlock);
	for(p = nil, r = reqfirst; r != nil; p = r, r = x){
		x = r->aux;
		if(r == req->oldreq){
			if(x == nil)
				reqlast = p;
			if(p == nil)
				reqfirst = x;
			else
				p->aux = x;
			r->aux = nil;
			respond(r, "interrupted");
			break;
		}
	}
	qunlock(&evlock);
	respond(req, nil);
}

Srv usbdsrv = {
	.attach = usbdattach,
	.walk1 = usbdwalk,
	.read = usbdread,
	.stat = usbdstat,
	.open = usbdopen,
	.flush = usbdflush,
	.destroyfid = usbddestroyfid,
};

static void
assignhname(Dev *dev)
{
	extern Hub *hubs;
	char buf[64];
	Usbdev *ud;
	Hub *h;
	int i;

	ud = dev->usb;

	/* build string of device unique stuff */
	snprint(buf, sizeof(buf), "%.4x%.4x%.4x%.6lx%s",
		ud->vid, ud->did, ud->dno, ud->csp, ud->serial);

	hname(buf);

	/* check for collisions */
	for(h = hubs; h != nil; h = h->next){
		for(i = 1; i <= h->nport; i++){
			if(h->port[i].dev == nil)
				continue;
			if(h->port[i].dev->hname == nil || h->port[i].dev == dev)
				continue;
			if(strcmp(h->port[i].dev->hname, buf) == 0){
				dev->hname = smprint("%s%d", buf, dev->id);
				return;
			}
		}
	}
	dev->hname = strdup(buf);
}

int
attachdev(Port *p)
{
	Dev *d = p->dev;

	if(d->usb->class == Clhub){
		/*
		 * Hubs are handled directly by this process avoiding
		 * concurrent operation so that at most one device
		 * has the config address in use.
		 * We cancel kernel debug for these eps. too chatty.
		 */
		if((p->hub = newhub(d->dir, d)) == nil)
			return -1;
		return 0;
	}

	/* close control endpoint so driver can open it */
	close(d->dfd);
	d->dfd = -1;

	/* assign stable name based on device descriptor */
	assignhname(d);

	/* set device info for ctl file */
	devctl(d, "info %s csp %#08lux vid %#.4ux did %#.4ux %q %q %s",
		classname(Class(d->usb->csp)), d->usb->csp, d->usb->vid, d->usb->did,
		d->usb->vendor, d->usb->product, d->hname);

	pushevent(d, formatdev(d, 0));
	return 0;
}

void
detachdev(Port *p)
{
	Dev *d = p->dev;

	if(d->usb->class == Clhub)
		return;
	pushevent(d, formatdev(d, 1));
}

/*
 * we create /env/usbbusy on startup and once all devices have been
 * enumerated and readers have consumed all the events, we remove the
 * file so nusbrc can continue.
 */
static int busyfd = -1;

void
checkidle(void)
{
	if(busyfd < 0 || reqlast == nil || evlast == nil || evlast->prev > 0)
		return;

	close(busyfd);
	busyfd = -1;
}

void
main(int argc, char **argv)
{
	int fd, i, nd;
	Dir *d;

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'd':
		usbdebug++;
		break;
	} ARGEND;

	busyfd = ocreate("/env/usbbusy", ORCLOSE, 0600);
	quotefmtinstall();
	initevent();
	rfork(RFNOTEG);
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1: sysfatal("rfork: %r");
	case 0: work(); exits(nil);
	}
	if(argc == 0){
		if((fd = open("/dev/usb", OREAD)) < 0){
			rendezvous(work, nil);
			sysfatal("/dev/usb: %r");
		}
		nd = dirreadall(fd, &d);
		close(fd);
		if(nd < 2){
			rendezvous(work, nil);
			sysfatal("/dev/usb: no hubs");
		}
		for(i = 0; i < nd; i++)
			if(strcmp(d[i].name, "ctl") != 0)
				rendezvous(work, smprint("/dev/usb/%s", d[i].name));
		free(d);
	}else
		for(i = 0; i < argc; i++)
			rendezvous(work, estrdup9p(argv[i]));
	rendezvous(work, nil);
	postsharesrv(&usbdsrv, nil, "usb", "usbd");
	exits(nil);
}
