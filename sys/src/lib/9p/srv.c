/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>
#include <auth.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>

void (*_forker)(void(*)(void*), void*, int);

static char Ebadoffset[] = "bad offset";
static char Ebotch[] = "9P protocol botch";
static char Ecreatenondir[] = "create in non-directory";
static char Edupfid[] = "duplicate fid";
static char Eduptag[] = "duplicate tag";
static char Eisdir[] = "is a directory";
static char Enocreate[] = "create prohibited";
static char Enoremove[] = "remove prohibited";
static char Enostat[] = "stat prohibited";
static char Enotfound[] = "file not found";
static char Enowrite[] = "write prohibited";
static char Enowstat[] = "wstat prohibited";
static char Eperm[] = "permission denied";
static char Eunknownfid[] = "unknown fid";
static char Ebaddir[] = "bad directory in wstat";
static char Ewalknodir[] = "walk in non-directory";
static char Etruncread[] = "truncate on read open";

static void
setfcallerror(Fcall *f, char *err)
{
	f->ename = err;
	f->type = Rerror;
}

static void
changemsize(Srv *srv, int msize)
{
	if(srv->rbuf && srv->wbuf && srv->msize == msize)
		return;
	qlock(&srv->rlock);
	qlock(&srv->wlock);
	srv->msize = msize;
	free(srv->rbuf);
	free(srv->wbuf);
	srv->rbuf = emalloc9p(msize);
	srv->wbuf = emalloc9p(msize);
	qunlock(&srv->rlock);
	qunlock(&srv->wlock);
}

static Req*
getreq(Srv *s)
{
	int32_t n;
	uint8_t *buf;
	Fcall f;
	Req *r;

	qlock(&s->rlock);
	while((n = read9pmsg(s->infd, s->rbuf, s->msize)) == 0)
		;
	if(n < 0){
		qunlock(&s->rlock);
		return nil;
	}

	buf = emalloc9p(n);
	memmove(buf, s->rbuf, n);
	qunlock(&s->rlock);

	if(convM2S(buf, n, &f) != n){
		free(buf);
		return nil;
	}

	if((r=allocreq(s->rpool, f.tag)) == nil){	/* duplicate tag: cons up a fake Req */
		r = emalloc9p(sizeof *r);
		incref(&r->ref);
		r->tag = f.tag;
		r->ifcall = f;
		r->error = Eduptag;
		r->buf = buf;
		r->responded = 0;
		r->type = 0;
		r->srv = s;
		r->pool = nil;
		if(chatty9p)
			fprint(2, "<-%d- %F: dup tag\n", s->infd, &f);
		return r;
	}

	r->srv = s;
	r->responded = 0;
	r->buf = buf;
	r->ifcall = f;
	memset(&r->ofcall, 0, sizeof r->ofcall);
	r->type = r->ifcall.type;

	if(chatty9p)
		if(r->error)
			fprint(2, "<-%d- %F: %s\n", s->infd, &r->ifcall, r->error);
		else
			fprint(2, "<-%d- %F\n", s->infd, &r->ifcall);

	return r;
}

static void
filewalk(Req *r)
{
	int i;
	File *f;

	f = r->fid->file;
	assert(f != nil);

	incref(&f->ref);
	for(i=0; i<r->ifcall.nwname; i++)
		if(f = walkfile(f, r->ifcall.wname[i]))
			r->ofcall.wqid[i] = f->qid;
		else
			break;

	r->ofcall.nwqid = i;
	if(f){
		r->newfid->file = f;
		r->newfid->qid = r->newfid->file->qid;
	}
	respond(r, nil);
}

void
walkandclone(Req *r, char *(*walk1)(Fid*, char*, void*), char *(*clone)(Fid*, Fid*, void*), void *arg)
{
	int i;
	char *e;

	if(r->fid == r->newfid && r->ifcall.nwname > 1){
		respond(r, "lib9p: unused documented feature not implemented");
		return;
	}

	if(r->fid != r->newfid){
		r->newfid->qid = r->fid->qid;
		if(clone && (e = clone(r->fid, r->newfid, arg))){
			respond(r, e);
			return;
		}
	}

	e = nil;
	for(i=0; i<r->ifcall.nwname; i++){
		if(e = walk1(r->newfid, r->ifcall.wname[i], arg))
			break;
		r->ofcall.wqid[i] = r->newfid->qid;
	}

	r->ofcall.nwqid = i;
	if(e && i==0)
		respond(r, e);
	else
		respond(r, nil);
}

static void
sversion(Srv *srv, Req *r)
{
	if(srv->rref.ref != 1){
		respond(r, Ebotch);
		return;
	}
	if(strncmp(r->ifcall.version, "9P", 2) != 0){
		r->ofcall.version = "unknown";
		respond(r, nil);
		return;
	}
	r->ofcall.version = "9P2000";
	if(r->ifcall.msize < 256){
		respond(r, "version: message size too small");
		return;
	}
	if(r->ifcall.msize < 1024*1024)
		r->ofcall.msize = r->ifcall.msize;
	else
		r->ofcall.msize = 1024*1024;
	respond(r, nil);
}

static void
rversion(Req *r, char *error)
{
	if(error == nil)
		changemsize(r->srv, r->ofcall.msize);
}

static void
sauth(Srv *srv, Req *r)
{
	char e[ERRMAX];

	if((r->afid = allocfid(srv->fpool, r->ifcall.afid)) == nil){
		respond(r, Edupfid);
		return;
	}
	if(srv->auth)
		srv->auth(r);
	else{
		snprint(e, sizeof e, "%s: authentication not required", argv0);
		respond(r, e);
	}
}
static void
rauth(Req *r, char *error)
{
	if(error && r->afid)
		closefid(removefid(r->srv->fpool, r->afid->fid));
}

static void
sattach(Srv *srv, Req *r)
{
	if((r->fid = allocfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Edupfid);
		return;
	}
	r->afid = nil;
	if(r->ifcall.afid != NOFID && (r->afid = lookupfid(srv->fpool, r->ifcall.afid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	r->fid->uid = estrdup9p(r->ifcall.uname);
	if(srv->tree){
		r->fid->file = srv->tree->root;
		incref(&r->fid->file->ref);
		r->ofcall.qid = r->fid->file->qid;
		r->fid->qid = r->ofcall.qid;
	}
	if(srv->attach)
		srv->attach(r);
	else
		respond(r, nil);
	return;
}
static void
rattach(Req *r, char *error)
{
	if(error && r->fid)
		closefid(removefid(r->srv->fpool, r->fid->fid));
}

static void
sflush(Srv *srv, Req *r)
{
	r->oldreq = lookupreq(srv->rpool, r->ifcall.oldtag);
	if(r->oldreq == nil || r->oldreq == r)
		respond(r, nil);
	else if(srv->flush)
		srv->flush(r);
	else
		respond(r, nil);
}
static int
rflush(Req *r, char *error)
{
	Req *or;

	assert(error == nil);
	or = r->oldreq;
	if(or){
		qlock(&or->lk);
		if(or->responded == 0){
			or->flush = erealloc9p(or->flush, (or->nflush+1)*sizeof(or->flush[0]));
			or->flush[or->nflush++] = r;
			qunlock(&or->lk);
			return -1;		/* delay response until or is responded */
		}
		qunlock(&or->lk);
		closereq(or);
	}
	r->oldreq = nil;
	return 0;
}

static char*
oldwalk1(Fid *fid, char *name, void *arg)
{
	char *e;
	Qid qid;
	Srv *srv;

	srv = arg;
	e = srv->walk1(fid, name, &qid);
	if(e)
		return e;
	fid->qid = qid;
	return nil;
}

static char*
oldclone(Fid *fid, Fid *newfid, void *arg)
{
	Srv *srv;

	srv = arg;
	if(srv->clone == nil)
		return nil;
	return srv->clone(fid, newfid);
}

static void
swalk(Srv *srv, Req *r)
{
	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if(r->fid->omode != -1){
		respond(r, "cannot clone open fid");
		return;
	}
	if(r->ifcall.nwname && !(r->fid->qid.type&QTDIR)){
		respond(r, Ewalknodir);
		return;
	}
	if(r->ifcall.fid != r->ifcall.newfid){
		if((r->newfid = allocfid(srv->fpool, r->ifcall.newfid)) == nil){
			respond(r, Edupfid);
			return;
		}
		r->newfid->uid = estrdup9p(r->fid->uid);
	}else{
		incref(&r->fid->ref);
		r->newfid = r->fid;
	}
	if(r->fid->file){
		filewalk(r);
	}else if(srv->walk1)
		walkandclone(r, oldwalk1, oldclone, srv);
	else if(srv->walk)
		srv->walk(r);
	else
		sysfatal("no walk function, no file trees");
}
static void
rwalk(Req *r, char *error)
{
	if(error || r->ofcall.nwqid < r->ifcall.nwname){
		if(r->ifcall.fid != r->ifcall.newfid && r->newfid)
			closefid(removefid(r->srv->fpool, r->newfid->fid));
		if (r->ofcall.nwqid==0){
			if(error==nil && r->ifcall.nwname!=0)
				r->error = Enotfound;
		}else
			r->error = nil;	// No error on partial walks
	}else{
		if(r->ofcall.nwqid == 0){
			/* Just a clone */
			r->newfid->qid = r->fid->qid;
		}else{
			/* if file trees are in use, filewalk took care of the rest */
			r->newfid->qid = r->ofcall.wqid[r->ofcall.nwqid-1];
		}
	}
}

static int
dirwritable(Fid *fid)
{
	File *f;

	f = fid->file;
	if(f){
		rlock(&f->rwl);
		if(f->parent && !hasperm(f->parent, fid->uid, AWRITE)){
			runlock(&f->rwl);
			return 0;
		}
		runlock(&f->rwl);
	}
	return 1;
}

static void
sopen(Srv *srv, Req *r)
{
	int p;

	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if(r->fid->omode != -1){
		respond(r, Ebotch);
		return;
	}
	if((r->fid->qid.type&QTDIR) && (r->ifcall.mode&~NP_ORCLOSE) != NP_OREAD){
		respond(r, Eisdir);
		return;
	}
	r->ofcall.qid = r->fid->qid;
	switch(r->ifcall.mode&3){
	default:
		assert(0);
	case NP_OREAD:
		p = AREAD;
		break;
	case NP_OWRITE:
		p = AWRITE;
		break;
	case NP_ORDWR:
		p = AREAD|AWRITE;
		break;
	case NP_OEXEC:
		p = AEXEC;
		break;
	}
	if(r->ifcall.mode&NP_OZEROES){
		/* According to http://man.cat-v.org/9front/5/open
		 * all bits in NP_ZEROES should be zero
		 */
		respond(r, Ebotch);
		return;
	}
	if((r->ifcall.mode&NP_OTRUNC) && (r->ifcall.mode&NP_OREAD)){
		/* In Jehanne the kernel doesn't check OTRUNC invariants:
		 * it's up to the receiving server/device to ensure that
		 * a call to NP_OREAD|NP_OTRUNC will fail
		 */
		respond(r, Etruncread);
		return;
	}
	if(r->ifcall.mode&NP_OTRUNC)
		p |= AWRITE;
	if((r->fid->qid.type&QTDIR) && p!=AREAD && p!=AEXIST){
		respond(r, Eperm);
		return;
	}
	if(r->fid->file){
		if(!hasperm(r->fid->file, r->fid->uid, p)){
			respond(r, Eperm);
			return;
		}
		if((r->ifcall.mode&NP_ORCLOSE) && !dirwritable(r->fid)){
			respond(r, Eperm);
			return;
		}
		r->ofcall.qid = r->fid->file->qid;
		if((r->ofcall.qid.type&QTDIR)
		&& (r->fid->rdir = opendirfile(r->fid->file)) == nil){
			respond(r, "opendirfile failed");
			return;
		}
	}
	if(srv->open)
		srv->open(r);
	else
		respond(r, nil);
}
static void
ropen(Req *r, char *error)
{
	char errbuf[ERRMAX];
	if(error)
		return;
	if(chatty9p){
		snprint(errbuf, sizeof errbuf, "fid mode is 0x%ux\n", r->ifcall.mode);
		jehanne_write(2, errbuf, strlen(errbuf));
	}
	r->fid->omode = r->ifcall.mode;
	r->fid->qid = r->ofcall.qid;
	if(r->ofcall.qid.type&QTDIR)
		r->fid->diroffset = 0;
}

static void
screate(Srv *srv, Req *r)
{
	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil)
		respond(r, Eunknownfid);
	else if(r->fid->omode != -1)
		respond(r, Ebotch);
	else if(!(r->fid->qid.type&QTDIR))
		respond(r, Ecreatenondir);
	else if(r->fid->file && !hasperm(r->fid->file, r->fid->uid, AWRITE))
		respond(r, Eperm);
	else if(srv->create)
		srv->create(r);
	else
		respond(r, Enocreate);
}
static void
rcreate(Req *r, char *error)
{
	if(error)
		return;
	r->fid->omode = r->ifcall.mode;
	r->fid->qid = r->ofcall.qid;
}

static void
sread(Srv *srv, Req *r)
{
	int o;

	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if((int)r->ifcall.count < 0){
		respond(r, Ebotch);
		return;
	}
	if(r->ifcall.offset < 0
	|| ((r->fid->qid.type&QTDIR) && r->ifcall.offset != 0 && r->ifcall.offset != r->fid->diroffset)){
		respond(r, Ebadoffset);
		return;
	}

	if(r->ifcall.count > srv->msize - IOHDRSZ)
		r->ifcall.count = srv->msize - IOHDRSZ;
	r->rbuf = emalloc9p(r->ifcall.count);
	r->ofcall.data = r->rbuf;
	o = r->fid->omode & 3;
	if(o != NP_OREAD && o != NP_ORDWR && o != NP_OEXEC){
		respond(r, Ebotch);
		return;
	}
	if((r->fid->qid.type&QTDIR) && r->fid->file){
		r->ofcall.count = readdirfile(r->fid->rdir, r->rbuf, r->ifcall.count);
		respond(r, nil);
		return;
	}
	if(srv->read)
		srv->read(r);
	else
		respond(r, "no srv->read");
}
static void
rread(Req *r, char *error)
{
	if(error==nil && (r->fid->qid.type&QTDIR))
		r->fid->diroffset += r->ofcall.count;
}

static void
swrite(Srv *srv, Req *r)
{
	int o;
	char e[ERRMAX];

	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if((int)r->ifcall.count < 0){
		respond(r, Ebotch);
		return;
	}
	if(r->ifcall.offset < 0){
		respond(r, Ebotch);
		return;
	}
	if(r->ifcall.count > srv->msize - IOHDRSZ)
		r->ifcall.count = srv->msize - IOHDRSZ;
	o = r->fid->omode & 3;
	if(o != NP_OWRITE && o != NP_ORDWR){
		snprint(e, sizeof e, "write on fid with open mode 0x%ux", r->fid->omode);
		respond(r, e);
		return;
	}
	if(srv->write)
		srv->write(r);
	else
		respond(r, Enowrite);
}
static void
rwrite(Req *r, char *error)
{
	if(error)
		return;
	if(r->fid->file)
		r->fid->file->qid.vers++;
}

static void
sclunk(Srv *srv, Req *r)
{
	if((r->fid = removefid(srv->fpool, r->ifcall.fid)) == nil)
		respond(r, Eunknownfid);
	else
		respond(r, nil);
}
static void
rclunk(Req* _, char* __)
{
}

static void
sremove(Srv *srv, Req *r)
{
	if((r->fid = removefid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if(!dirwritable(r->fid)){
		respond(r, Eperm);
		return;
	}
	if(srv->remove)
		srv->remove(r);
	else
		respond(r, r->fid->file ? nil : Enoremove);
}
static void
rremove(Req *r, char *error, char *errbuf)
{
	if(error)
		return;
	if(r->fid->file){
		if(removefile(r->fid->file) < 0){
			snprint(errbuf, ERRMAX, "remove %s: %r",
				r->fid->file->name);
			r->error = errbuf;
		}
		r->fid->file = nil;
	}
}

static void
sstat(Srv *srv, Req *r)
{
	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if(r->fid->file){
		/* should we rlock the file? */
		r->d = r->fid->file->Dir;
		if(r->d.name)
			r->d.name = estrdup9p(r->d.name);
		if(r->d.uid)
			r->d.uid = estrdup9p(r->d.uid);
		if(r->d.gid)
			r->d.gid = estrdup9p(r->d.gid);
		if(r->d.muid)
			r->d.muid = estrdup9p(r->d.muid);
	}
	if(srv->stat)
		srv->stat(r);
	else if(r->fid->file)
		respond(r, nil);
	else
		respond(r, Enostat);
}
static void
rstat(Req *r, char *error)
{
	int n;
	uint8_t *statbuf;
	uint8_t tmp[BIT16SZ];

	if(error)
		return;
	if(convD2M(&r->d, tmp, BIT16SZ) != BIT16SZ){
		r->error = "convD2M(_,_,BIT16SZ) did not return BIT16SZ";
		return;
	}
	n = GBIT16(tmp)+BIT16SZ;
	statbuf = emalloc9p(n);
	if(statbuf == nil){
		r->error = "out of memory";
		return;
	}
	r->ofcall.nstat = convD2M(&r->d, statbuf, n);
	r->ofcall.stat = statbuf;	/* freed in closereq */
	if(r->ofcall.nstat <= BIT16SZ){
		r->error = "convD2M fails";
		free(statbuf);
		return;
	}
}

static void
swstat(Srv *srv, Req *r)
{
	if((r->fid = lookupfid(srv->fpool, r->ifcall.fid)) == nil){
		respond(r, Eunknownfid);
		return;
	}
	if(srv->wstat == nil){
		respond(r, Enowstat);
		return;
	}
	if(convM2D(r->ifcall.stat, r->ifcall.nstat, &r->d, (char*)r->ifcall.stat) != r->ifcall.nstat){
		respond(r, Ebaddir);
		return;
	}
	if(r->d.qid.path != ~0 && r->d.qid.path != r->fid->qid.path){
		respond(r, "wstat -- attempt to change qid.path");
		return;
	}
	if(r->d.qid.vers != ~0 && r->d.qid.vers != r->fid->qid.vers){
		respond(r, "wstat -- attempt to change qid.vers");
		return;
	}
	if(r->d.mode != ~0){
		if(r->d.mode & ~(DMDIR|DMAPPEND|DMEXCL|DMTMP|0777)){
			respond(r, "wstat -- unknown bits in mode");
			return;
		}
		if(r->d.qid.type != (uint8_t)~0 && r->d.qid.type != ((r->d.mode>>24)&0xFF)){
			respond(r, "wstat -- qid.type/mode mismatch");
			return;
		}
		if(((r->d.mode>>24) ^ r->fid->qid.type) & ~(QTAPPEND|QTEXCL|QTTMP)){
			respond(r, "wstat -- attempt to change qid.type");
			return;
		}
	} else {
		if(r->d.qid.type != (uint8_t)~0 && r->d.qid.type != r->fid->qid.type){
			respond(r, "wstat -- attempt to change qid.type");
			return;
		}
	}
	srv->wstat(r);
}
static void
rwstat(Req* _, char* __)
{
}

static void srvclose(Srv *);

static void
srvwork(void *v)
{
	Srv *srv = v;
	Req *r;

	while(r = getreq(srv)){
		incref(&srv->rref);
		if(r->error){
			respond(r, r->error);
			continue;
		}
		qlock(&srv->slock);
		switch(r->ifcall.type){
		default:
			respond(r, "unknown message");
			break;
		case Tversion:	sversion(srv, r);	break;
		case Tauth:	sauth(srv, r);	break;
		case Tattach:	sattach(srv, r);	break;
		case Tflush:	sflush(srv, r);	break;
		case Twalk:	swalk(srv, r);	break;
		case Topen:	sopen(srv, r);	break;
		case Tcreate:	screate(srv, r);	break;
		case Tread:	sread(srv, r);	break;
		case Twrite:	swrite(srv, r);	break;
		case Tclunk:	sclunk(srv, r);	break;
		case Tremove:	sremove(srv, r);	break;
		case Tstat:	sstat(srv, r);	break;
		case Twstat:	swstat(srv, r);	break;
		}
		qunlock(&srv->slock);
	}

	if(srv->end && srv->sref.ref == 1)
		srv->end(srv);
	if(decref(&srv->sref) == 0)
		srvclose(srv);
}

static void
srvclose(Srv *srv)
{
	if(srv->rref.ref || srv->sref.ref)
		return;

	if(chatty9p)
		fprint(2, "srvclose\n");

	free(srv->rbuf);
	srv->rbuf = nil;
	free(srv->wbuf);
	srv->wbuf = nil;
	srv->msize = 0;
	freefidpool(srv->fpool);
	srv->fpool = nil;
	freereqpool(srv->rpool);
	srv->rpool = nil;

	if(srv->free)
		srv->free(srv);
}

void
srvacquire(Srv *srv)
{
	incref(&srv->sref);
	qlock(&srv->slock);
}

void
srvrelease(Srv *srv)
{
	if(decref(&srv->sref) == 0){
		incref(&srv->sref);
		_forker(srvwork, srv, 0);
	}
	qunlock(&srv->slock);
}

void
srv(Srv *srv)
{
	fmtinstall('D', dirfmt);
	fmtinstall('F', fcallfmt);

	srv->sref.ref = 0;
	srv->rref.ref = 0;

	if(srv->fpool == nil)
		srv->fpool = allocfidpool(srv->destroyfid);
	if(srv->rpool == nil)
		srv->rpool = allocreqpool(srv->destroyreq);
	if(srv->msize == 0)
		srv->msize = 8192+IOHDRSZ;

	changemsize(srv, srv->msize);

	srv->fpool->srv = srv;
	srv->rpool->srv = srv;

	if(srv->start)
		srv->start(srv);

	incref(&srv->sref);
	srvwork(srv);
}

void
respond(Req *r, char *error)
{
	int i, m, n;
	char errbuf[ERRMAX];
	Srv *srv;

	srv = r->srv;
	assert(srv != nil);

	assert(r->responded == 0);
	r->error = error;

	switch(r->ifcall.type){
	/*
	 * Flush is special.  If the handler says so, we return
	 * without further processing.  Respond will be called
	 * again once it is safe.
	 */
	case Tflush:
		if(rflush(r, error)<0)
			return;
		break;
	case Tversion:
		rversion(r, error);
		break;
	case Tauth:
		rauth(r, error);
		break;
	case Tattach:
		rattach(r, error);
		break;
	case Twalk:
		rwalk(r, error);
		break;
	case Topen:
		ropen(r, error);
		break;
	case Tcreate:
		rcreate(r, error);
		break;
	case Tread:
		rread(r, error);
		break;
	case Twrite:
		rwrite(r, error);
		break;
	case Tclunk:
		rclunk(r, error);
		break;
	case Tremove:
		rremove(r, error, errbuf);
		break;
	case Tstat:
		rstat(r, error);
		break;
	case Twstat:
		rwstat(r, error);
		break;
	default:
		/* nothing to do */
		break;
	}

	r->ofcall.tag = r->ifcall.tag;
	r->ofcall.type = r->ifcall.type+1;
	if(r->error)
		setfcallerror(&r->ofcall, r->error);

	if(chatty9p)
		fprint(2, "-%d-> %F\n", srv->outfd, &r->ofcall);

	qlock(&srv->wlock);
	n = convS2M(&r->ofcall, srv->wbuf, srv->msize);
	if(n <= 0){
		fprint(2, "msize = %d n = %d %F\n", srv->msize, n, &r->ofcall);
		abort();
	}
	assert(n > 2);
	if(r->pool)	/* not a fake */
		closereq(removereq(r->pool, r->ifcall.tag));
	m = jehanne_write(srv->outfd, srv->wbuf, n);
	if(m != n)
		fprint(2, "lib9p srv: write %d returned %d on fd %d: %r", n, m, srv->outfd);
	qunlock(&srv->wlock);

	qlock(&r->lk);	/* no one will add flushes now */
	r->responded = 1;
	qunlock(&r->lk);

	for(i=0; i<r->nflush; i++)
		respond(r->flush[i], nil);
	free(r->flush);
	r->flush = nil;
	r->nflush = 0;

	if(r->pool)
		closereq(r);
	else
		free(r);

	if(decref(&srv->rref) == 0)
		srvclose(srv);
}

void
responderror(Req *r)
{
	char errbuf[ERRMAX];

	rerrstr(errbuf, sizeof errbuf);
	respond(r, errbuf);
}

int
postfd(char *name, int pfd)
{
	int fd;
	char buf[80];

	snprint(buf, sizeof buf, "/srv/%s", name);
	if(chatty9p)
		fprint(2, "postfd %s\n", buf);
	fd = ocreate(buf, OWRITE|ORCLOSE|OCEXEC, 0600);
	if(fd < 0){
		if(chatty9p)
			fprint(2, "create fails: %r\n");
		return -1;
	}
	if(fprint(fd, "%d", pfd) < 0){
		if(chatty9p)
			fprint(2, "write fails: %r\n");
		sys_close(fd);
		return -1;
	}
	if(chatty9p)
		fprint(2, "postfd successful\n");
	return 0;
}

int
sharefd(char *name, char *desc, int pfd)
{
	int fd;
	char buf[80];

	snprint(buf, sizeof buf, "#σc/%s", name);
	if((fd = sys_create(buf, OREAD, 0700|DMDIR)) >= 0)
		sys_close(fd);
	snprint(buf, sizeof buf, "#σc/%s/%s", name, desc);
	if(chatty9p)
		fprint(2, "sharefd %s\n", buf);
	fd = ocreate(buf, OWRITE, 0600);
	if(fd < 0){
		if(chatty9p)
			fprint(2, "create fails: %r\n");
		return -1;
	}
	if(fprint(fd, "%d\n", pfd) < 0){
		if(chatty9p)
			fprint(2, "write fails: %r\n");
		sys_close(fd);
		return -1;
	}
	sys_close(fd);
	if(chatty9p)
		fprint(2, "sharefd successful\n");
	return 0;
}
