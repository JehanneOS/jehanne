#include <u.h>
#include <lib9.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Dev *devs;

void
devwork(void *v)
{
	Dev *d;
	Buf *b;
	Channel *r;
	uint8_t buf[BLOCK];
	
	d = v;
	for(;;){
		qlock(&d->workl);
		while(d->work.wnext == &d->work)
			rsleep(&d->workr);
		b = d->work.wnext;
		b->wnext->wprev = b->wprev;
		b->wprev->wnext = b->wnext;
		b->wnext = b->wprev = nil;
		qunlock(&d->workl);
		if(b->d == nil) /* this is a sync request */
			goto reply;
		if(b->off >= d->size){
			b->error = Einval;
			goto reply;
		}
		b->error = nil;
		if(b->op & BWRITE){
			memset(buf, 0, sizeof(buf));
			pack(b, buf);
			if(pwrite(d->fd, buf, BLOCK, b->off*BLOCK) < BLOCK){
				dprint("write: %r\n");
				b->error = Eio;
			}
		}else{
			int n, m;

			for(n = 0; n < BLOCK; n += m){
				m = pread(d->fd, buf+n, BLOCK-n, b->off*BLOCK+n);
				if(m < 0)
					dprint("read: %r\n");
				if(m <= 0)
					break;
			}
			if(n < BLOCK)
				b->error = Eio;
			else
				unpack(b, buf);
		}
	reply:
		r = b->resp;
		b->resp = nil;
		if(r != nil)
			send(r, &b);
	}
}

Dev *
newdev(char *file)
{
	Dev *d, **e;
	Dir *dir;
	Buf *b;
	
	d = emalloc(sizeof(*d));
	d->fd = open(file, ORDWR);
	if(d->fd < 0){
		free(d);
		return nil;
	}
	dir = dirfstat(d->fd);
	if(dir == nil){
	error:
		close(d->fd);
		free(d);
		return nil;
	}
	d->size = dir->length / BLOCK;
	free(dir);
	if(d->size == 0){
		werrstr("device file too short");
		goto error;
	}
	d->name = estrdup(file);
	for(b = d->buf; b < d->buf + BUFHASH + 1; b++)
		b->dnext = b->dprev = b;
	d->workr.l = &d->workl;
	d->work.wnext = d->work.wprev = &d->work;
	proccreate(devwork, d, mainstacksize);
	for(e = &devs; *e != nil; e = &(*e)->next)
		;
	*e = d;
	return d;
}
