#include <u.h>
#include <lib9.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Channel *getb, *putb, *syncb;
Buf bfree;
BufReq *freereq, *freereqlast;

static void
markbusy(Buf *b)
{
	b->busy = 1;
	if(b->fnext != nil){
		b->fnext->fprev = b->fprev;
		b->fprev->fnext = b->fnext;
		b->fnext = b->fprev = nil;
	}
}

static void
markfree(Buf *b)
{
	b->busy = 0;
	b->fnext = &bfree;
	b->fprev = bfree.fprev;
	b->fnext->fprev = b;
	b->fprev->fnext = b;
}

static void
changedev(Buf *b, Dev *d, uint64_t off)
{
	if(b->dnext != nil){
		b->dnext->dprev = b->dprev;
		b->dprev->dnext = b->dnext;
		b->dprev = nil;
		b->dnext = nil;
	}
	b->off = off;
	b->d = d;
	if(d != nil){
		b->dnext = &d->buf[b->off & BUFHASH];
		b->dprev = b->dnext->dprev;
		b->dnext->dprev = b;
		b->dprev->dnext = b;
	}
}

static void
delayreq(BufReq req, BufReq **first, BufReq **last)
{
	BufReq *r;

	r = emalloc(sizeof(*r));
	*r = req;
	r->next = nil;
	if(*first == nil)
		*first = r;
	else
		(*last)->next = r;
	*last = r;
}

static BufReq
undelayreq(BufReq **first, BufReq **last)
{
	BufReq r;
	
	r = **first;
	free(*first);
	*first = r.next;
	if(r.next == nil)
		*last = nil;
	return r;
}

static void
work(Dev *d, Buf *b)
{
	qlock(&d->workl);
	b->wnext = &d->work;
	b->wprev = b->wnext->wprev;
	b->wnext->wprev = b;
	b->wprev->wnext = b;
	rwakeup(&d->workr);
	qunlock(&d->workl);
}

static void
givebuf(BufReq req, Buf *b)
{
	Buf *c, *l;

again:
	if(req.d == b->d && req.off == b->off){
		markbusy(b);
		send(req.resp, &b);
		return;
	}
	l = &req.d->buf[req.off & BUFHASH];
	for(c = l->dnext; c != l; c = c->dnext)
		if(c->off == req.off){
			if(c->busy)
				delayreq(req, &c->next, &c->last);
			else{
				markbusy(c);
				send(req.resp, &c);
			}
			if(b->next == nil)
				return;
			req = undelayreq(&b->next, &b->last);
			goto again;
		}
	if(b->next != nil){
		givebuf(undelayreq(&b->next, &b->last), b);
		b = bfree.fnext;
	}
	if(b == &bfree){
		delayreq(req, &freereq, &freereqlast);
		return;
	}
	markbusy(b);
	if(b->d != nil && b->op & BDELWRI){
		delayreq(req, &b->next, &b->last);
		b->op &= ~BDELWRI;
		b->op |= BWRITE;
		b->resp = putb;
		work(b->d, b);
		return;
	}
	changedev(b, req.d, req.off);
	b->op &= ~(BWRITE|BDELWRI|BWRIM);
	if(req.nodata)
		send(req.resp, &b);
	else{
		b->resp = req.resp;
		work(b->d, b);
	}
}

static void
handleget(BufReq req)
{
	givebuf(req, bfree.fnext);
}

static void
handleput(Buf *b)
{
	if(b->op & BWRIM){
		b->op &= ~(BWRIM | BDELWRI);
		b->op |= BWRITE;
		b->resp = putb;
		work(b->d, b);
		return;
	}
	if(b->error != nil){
		b->error = nil;
		b->op &= ~BDELWRI;
		changedev(b, nil, -1);
	}
	b->op &= ~BWRITE;
	markfree(b);
	if(b->next != nil)
		givebuf(undelayreq(&b->next, &b->last), b);
	else if(freereq != nil)
		givebuf(undelayreq(&freereq, &freereqlast), b);
}

static void
handlesync(Channel *resp)
{
	Buf *b, *c;

	for(b = bfree.fnext; b != &bfree; b = c){
		c = b->fnext;
		if(b->d != nil && b->op & BDELWRI){
			markbusy(b);
			b->resp = putb;
			b->op &= ~BDELWRI;
			b->op |= BWRITE;
			work(b->d, b);
		}
	}
	if(resp != nil)
		sendp(resp, nil);
}

static void
bufproc(void* _)
{
	BufReq req;
	Buf *buf;
	Channel *r;
	Alt a[] = {{getb, &req, CHANRCV}, {putb, &buf, CHANRCV}, {syncb, &r, CHANRCV}, {nil, nil, CHANEND}};

	workerinit();
	for(;;)
		switch(alt(a)){
		case 0:
			handleget(req);
			break;
		case 1:
			handleput(buf);
			break;
		case 2:
			handlesync(r);
			break;
		case -1:
			sysfatal("alt: %r");
		}
}

static char *
typenames[] = {
	[TRAW] "raw",
	[TSUPERBLOCK] "superblock",
	[TDENTRY] "dentry",
	[TINDIR] "indir",
	[TREF] "ref",
	nil
};

static int
Tfmt(Fmt *f)
{
	int t;
	
	t = va_arg(f->args, uint);
	if(t >= nelem(typenames) || typenames[t] == nil)
		return fmtprint(f, "??? (%d)", t);
	return fmtstrcpy(f, typenames[t]);
}

void
bufinit(int nbuf)
{
	Buf *b;

	fmtinstall('T', Tfmt);
	b = emalloc(sizeof(*b) * nbuf);
	bfree.fnext = bfree.fprev = &bfree;
	while(nbuf--)
		markfree(b++);
	getb = chancreate(sizeof(BufReq), 0);
	putb = chancreate(sizeof(Buf *), 32);
	syncb = chancreate(sizeof(void*), 0);
	proccreate(bufproc, nil, mainstacksize);
}

Buf *
getbuf(Dev *d, uint64_t off, int type, int nodata)
{
	ThrData *th;
	BufReq req;
	Buf *b;

	if(off >= d->size)
		abort();
	th = getthrdata();
	req.d = d;
	req.off = off;
	req.resp = th->resp;
	req.nodata = nodata;
	send(getb, &req);
	recv(th->resp, &b);
	if(b->error != nil){
		werrstr("%s", b->error);
		putbuf(b);
		return nil;
	}
	if(nodata)
		b->type = type;
	if(b->type != type && type != TDONTCARE){
		dprint("type mismatch, dev %s, block %lld, got %T, want %T, caller %#p\n",
			d->name, off, b->type, type, getcallerpc());
		werrstr("phase error -- type mismatch");
		putbuf(b);
		return nil;
	}
	b->callerpc = getcallerpc();
	return b;
}

void
putbuf(Buf *b)
{
	send(putb, &b);
}

void
sync(int wait)
{
	Channel *r;
	Dev *d;
	Buf b;

	r = nil;
	if(wait)
		r = getthrdata()->resp;
	sendp(syncb, r);
	memset(&b, 0, sizeof(Buf));
	if(wait){
		recvp(r);
		for(d = devs; d != nil; d = d->next){
			b.d = nil;
			b.resp = r;
			b.busy = 1;
			work(d, &b);
			recvp(r);
		}
	}
}
