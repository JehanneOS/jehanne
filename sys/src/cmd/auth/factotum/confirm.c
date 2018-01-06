/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "dat.h"

Logbuf confbuf;

Req *cusewait;		/* requests waiting for confirmation */
Req **cuselast = &cusewait;

void
confirmread(Req *r)
{
	logbufread(&confbuf, r);
}

void
confirmflush(Req *r)
{
	Req **l;

	qlock(&confbuf);
	for(l=&cusewait; *l; l=(Req **)(&(*l)->aux)){
		if(*l == r){
			*l = r->aux;
			if(r->aux == nil)
				cuselast = l;
			r->aux = nil;
			qunlock(&confbuf);
			respond(r, "interrupted");
			return;
		}
	}
	qunlock(&confbuf);
	logbufflush(&confbuf, r);
}

static int
hastag(Fsstate *fss, int tag, int *tagoff)
{
	int i;

	for(i=0; i<fss->nconf; i++)
		if(fss->conf[i].tag == tag){
			*tagoff = i;
			return 1;
		}
	return 0;
}

int
confirmwrite(Srv *srv, char *s)
{
	char *t, *ans;
	int allow, tagoff;
	uint32_t tag;
	Attr *a;
	Fsstate *fss;
	Req *r, **l;

	a = _parseattr(s);
	if(a == nil){
		_freeattr(a);
		werrstr("empty write");
		return -1;
	}
	if((t = _strfindattr(a, "tag")) == nil){
		_freeattr(a);
		werrstr("no tag");
		return -1;
	}
	tag = strtoul(t, 0, 0);
	if((ans = _strfindattr(a, "answer")) == nil){
		_freeattr(a);
		werrstr("no answer");
		return -1;
	}
	if(strcmp(ans, "yes") == 0)
		allow = 1;
	else if(strcmp(ans, "no") == 0)
		allow = 0;
	else{
		_freeattr(a);
		werrstr("bad answer");
		return -1;
	}
	_freeattr(a);
	srvrelease(srv);
	qlock(&confbuf);
	fss = nil;
	tagoff = -1;
	for(l=&cusewait; (r = *l) != nil; l=(Req**)(&(*l)->aux)){
		fss = r->fid->aux;
		if(hastag(fss, tag, &tagoff)){
			*l = r->aux;
			if(r->aux == nil)
				cuselast = l;
			r->aux = nil;
			break;
		}
	}
	qunlock(&confbuf);
	if(r == nil || tagoff == -1){
		srvacquire(srv);
		werrstr("tag not found");
		return -1;
	}
	qlock(fss);
	fss->conf[tagoff].canuse = allow;
	rpcread(r);
	qunlock(fss);
	srvacquire(srv);
	return 0;
}

void
confirmqueue(Req *r, Fsstate *fss)
{
	int i, n;
	char msg[1024];

	if(*confirminuse == 0){
		respond(r, "confirm is closed");
		return;
	}

	n = 0;
	for(i=0; i<fss->nconf; i++)
		if(fss->conf[i].canuse == -1){
			n++;
			snprint(msg, sizeof msg, "confirm tag=%lud %A", fss->conf[i].tag, fss->conf[i].key->attr);
			logbufappend(&confbuf, msg);
		}
	if(n == 0){
		respond(r, "no confirmations to wait for (bug)");
		return;
	}
	qlock(&confbuf);
	r->aux = nil;
	*cuselast = r;
	cuselast = (Req **)&r->aux;
	qunlock(&confbuf);
}

/* Yes, I am unhappy that the code below is a copy of the code above. */

Logbuf needkeybuf;
Req *needwait;		/* requests that need keys */
Req **needlast = &needwait;

void
needkeyread(Req *r)
{
	logbufread(&needkeybuf, r);
}

void
needkeyflush(Req *r)
{
	Req **l;

	qlock(&needkeybuf);
	for(l=&needwait; *l; l=(Req**)(&(*l)->aux)){
		if(*l == r){
			*l = r->aux;
			if(r->aux == nil)
				needlast = l;
			r->aux = nil;
			qunlock(&needkeybuf);
			respond(r, "interrupted");
			return;
		}
	}
	qunlock(&needkeybuf);
	logbufflush(&needkeybuf, r);
}

int
needkeywrite(Srv *srv, char *s)
{
	char *t;
	uint32_t tag;
	Attr *a;
	Req *r, **l;
	Fsstate *fss;

	a = _parseattr(s);
	if(a == nil){
		_freeattr(a);
		werrstr("empty write");
		return -1;
	}
	if((t = _strfindattr(a, "tag")) == nil){
		_freeattr(a);
		werrstr("no tag");
		return -1;
	}
	tag = strtoul(t, 0, 0);
	r = nil;
	qlock(&needkeybuf);
	for(l=&needwait; *l; l=(Req**)(&(*l)->aux)){
		r = *l;
		if(r->tag == tag){
			*l = r->aux;
			if(r->aux == nil)
				needlast = l;
			r->aux = nil;
			break;
		}
	}
	qunlock(&needkeybuf);
	if(r == nil){
		_freeattr(a);
		werrstr("tag not found");
		return -1;
	}
	fss = r->fid->aux;
	srvrelease(srv);
	qlock(fss);
	if(s = _strfindattr(a, "error")){
		werrstr("%s", s);
		retrpc(r, RpcErrstr, fss);
	}else
		rpcread(r);
	_freeattr(a);
	qunlock(fss);
	srvacquire(srv);
	return 0;
}

int
needkeyqueue(Req *r, Fsstate *fss)
{
	char msg[1024];

	if(*needkeyinuse == 0)
		return -1;

	snprint(msg, sizeof msg, "needkey tag=%lud %s", r->tag, fss->keyinfo);
	logbufappend(&needkeybuf, msg);

	qlock(&needkeybuf);
	r->aux = nil;
	*needlast = r;
	needlast = (Req**)&r->aux;
	qunlock(&needkeybuf);

	return 0;
}

