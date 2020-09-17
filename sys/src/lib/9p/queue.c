#include <u.h>
#include <lib9.h>
#include <thread.h>
#include <9P2000.h>
#include <9p.h>

static void
_reqqueueproc(void *v)
{
	Reqqueue *q;
	Req *r;
	void (*f)(Req *);
	int fd;
	char *buf;

	q = v;
	sys_rfork(RFNOTEG);

	buf = smprint("/proc/%d/ctl", getpid());
	fd = sys_open(buf, OWRITE);
	free(buf);
	
	for(;;){
		qlock(&q->ql);
		q->flush = 0;
		if(fd >= 0)
			jehanne_write(fd, "nointerrupt", 11);
		q->cur = nil;
		while(q->next == (Queueelem *)q)
			rsleep(&q->r);
		r = (Req*)(((char*)q->next) - ((char*)&((Req*)0)->qu));
		r->qu.next->prev = r->qu.prev;
		r->qu.prev->next = r->qu.next;
		f = r->qu.f;
		memset(&r->qu, 0, sizeof(r->qu));
		q->cur = r;
		qunlock(&q->ql);
		if(f == nil)
			break;
		f(r);
	}

	free(r);
	free(q);
	threadexits(nil);
}

Reqqueue *
reqqueuecreate(void)
{
	Reqqueue *q;

	q = emalloc9p(sizeof(*q));
	memset(q, 0, sizeof(*q));
	q->r.l = &q->ql;
	q->next = q->prev = q;
	q->pid = proccreate(_reqqueueproc, q, mainstacksize);
	return q;
}

void
reqqueuepush(Reqqueue *q, Req *r, void (*f)(Req *))
{
	qlock(&q->ql);
	r->qu.f = f;
	r->qu.next = q;
	r->qu.prev = q->prev;
	q->prev->next = &r->qu;
	q->prev = &r->qu;
	rwakeup(&q->r);
	qunlock(&q->ql);
}

void
reqqueueflush(Reqqueue *q, Req *r)
{
	qlock(&q->ql);
	if(q->cur == r){
		threadint(q->pid);
		q->flush++;
		qunlock(&q->ql);
	}else{
		if(r->qu.next != nil){
			r->qu.next->prev = r->qu.prev;
			r->qu.prev->next = r->qu.next;
		}
		memset(&r->qu, 0, sizeof(r->qu));
		qunlock(&q->ql);
		respond(r, "interrupted");
	}
}

void
reqqueuefree(Reqqueue *q)
{
	Req *r;

	if(q == nil)
		return;
	r = emalloc9p(sizeof(Req));
	reqqueuepush(q, r, nil);
}
