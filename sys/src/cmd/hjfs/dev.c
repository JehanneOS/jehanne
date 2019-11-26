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
			if(sys_pwrite(d->fd, buf, BLOCK, b->off*BLOCK) < BLOCK){
				dprint("write: %r\n");
				b->error = Eio;
			}
		}else{
			int n, m;

			for(n = 0; n < BLOCK; n += m){
				m = sys_pread(d->fd, buf+n, BLOCK-n, b->off*BLOCK+n);
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
	d->fd = sys_open(file, ORDWR);
	if(d->fd < 0){
		free(d);
		return nil;
	}
	dir = dirfstat(d->fd);
	if(dir == nil){
	error:
		sys_close(d->fd);
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
