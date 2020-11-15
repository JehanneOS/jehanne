/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * References are managed as follows:
 * The channel to the server - a network connection or pipe - has one
 * reference for every Chan open on the server.  The server channel has
 * c->mux set to the Mnt used for muxing control to that server.  Mnts
 * have no reference count; they go away when c goes away.
 * Each channel derived from the mount point has mchan set to c,
 * and increfs/decrefs mchan to manage references on the server
 * connection.
 */

#define MAXRPC (IOHDRSZ+8192)

struct Mntrpc
{
	Chan*	c;		/* Channel for whom we are working */
	Mntrpc*	list;		/* Free/pending list */
	Fcall	request;	/* Outgoing file system protocol message */
	Fcall 	reply;		/* Incoming reply */
	Mnt*	mnt;		/* Mount device during rpc */
	Rendez*	z;		/* Place to hang out */
	Block*	w;		/* copy of write rpc for cache */
	Block*	b;		/* reply blocks */
	Mntrpc*	flushed;	/* message this one flushes */
	char	done;		/* Rpc completed */
};

enum
{
	TAGSHIFT = 5,			/* uint32_t has to be 32 bits */
	TAGMASK = (1<<TAGSHIFT)-1,
	NMASK = (64*1024)>>TAGSHIFT,
};

struct Mntalloc
{
	Lock;
	Mnt*		list;		/* Mount devices in use */
	Mnt*		mntfree;	/* Free list */
	Mntrpc*		rpcfree;
	uint32_t	nrpcfree;
	uint32_t	nrpcused;
	uint32_t	id;
	uint32_t	tagmask[NMASK];
}mntalloc;

static Chan*	mntchan(void);
static Mnt*	mntchk(Chan*);
static void	mntdirfix(uint8_t*, Chan*);
static Mntrpc*	mntflushalloc(Mntrpc*);
static Mntrpc*	mntflushfree(Mnt*, Mntrpc*);
static void	mntfree(Mntrpc*);
static void	mntgate(Mnt*);
static void	mntqrm(Mnt*, Mntrpc*);
static Mntrpc*	mntralloc(Chan*);
static long	mntrdwr(int, Chan*, void*, long, int64_t);
static int	mntrpcread(Mnt*, Mntrpc*);
static void	mountio(Mnt*, Mntrpc*);
static void	mountmux(Mnt*, Mntrpc*);
static void	mountrpc(Mnt*, Mntrpc*);
static int	rpcattn(void*);

char	Esbadstat[] = "invalid directory entry received from server";
char	Enoversion[] = "version not established for mount channel";


static void
mntreset(void)
{
	mntalloc.id = 1;
	mntalloc.tagmask[0] = 1;			/* don't allow 0 as a tag */
	mntalloc.tagmask[NMASK-1] = 0x80000000UL;	/* don't allow NOTAG */
	fmtinstall('F', fcallfmt);
	fmtinstall('D', dirfmt);
/* We can't install %M since eipfmt does and is used in the kernel [sape] */

}

/*
 * Version is not multiplexed: message sent only once per connection.
 */
usize
mntversion(Chan *c, uint32_t msize, char *version, usize returnlen)
{
	Fcall f;
	uint8_t *msg;
	Mnt *mnt;
	char *v;
	Queue *q;
	int k, l;
	unsigned long oo;
	char buf[128];

	eqlock(&c->umqlock);	/* make sure no one else does this until we've established ourselves */
	if(waserror()){
		qunlock(&c->umqlock);
		nexterror();
	}

	/* defaults */
	if(msize == 0)
		msize = MAXRPC;
	if(msize > c->iounit && c->iounit != 0)
		msize = c->iounit;
	v = version;
	if(v == nil || v[0] == '\0')
		v = VERSION9P;

	/* validity */
//	if(msize < 0)		// msize is unsigned, this is always false
//		error("bad iounit in version call");
	if(strncmp(v, VERSION9P, strlen(VERSION9P)) != 0)
		error("bad 9P version specification");

	mnt = c->mux;

	if(mnt != nil){
		qunlock(&c->umqlock);
		poperror();

		strecpy(buf, buf+sizeof buf, mnt->version);
		k = strlen(buf);
		if(strncmp(buf, v, k) != 0){
			snprint(buf, sizeof buf, "incompatible 9P versions %s %s", mnt->version, v);
			error(buf);
		}
		if(returnlen > 0){
			if(returnlen < k)
				error(Eshort);
			memmove(version, buf, k);
		}
		return k;
	}

	f.type = Tversion;
	f.tag = NOTAG;
	f.msize = msize;
	f.version = v;
	msg = malloc(MAXRPC);
	if(msg == nil)
		exhausted("version memory");
	if(waserror()){
		free(msg);
		nexterror();
	}
	k = convS2M(&f, msg, MAXRPC);
	if(k == 0)
		error("bad fversion conversion on send");

	lock(&c->l);
	oo = c->offset;
	c->offset += k;
	unlock(&c->l);

	l = c->dev->write(c, msg, k, oo);
	if(l < k){
		lock(&c->l);
		c->offset -= k - l;
		unlock(&c->l);
		error("short write in fversion");
	}

	/* message sent; receive and decode reply */
	for(k = 0; k < BIT32SZ || (k < GBIT32(msg) && k < 8192+IOHDRSZ); k += l){
		l = c->dev->read(c, msg+k, 8192+IOHDRSZ-k, c->offset);
		if(l <= 0)
			error("EOF receiving fversion reply");
		lock(&c->l);
		c->offset += l;
		unlock(&c->l);
	}

	l = convM2S(msg, k, &f);
	if(l != k)
		error("bad fversion conversion on reply");
	if(f.type != Rversion){
		if(f.type == Rerror)
			error(f.ename);
		error("unexpected reply type in fversion");
	}
	if(f.msize > msize)
		error("server tries to increase msize in fversion");
	if(f.msize<256 || f.msize>1024*1024)
		error("nonsense value of msize in fversion");
	k = strlen(f.version);
	if(strncmp(f.version, v, k) != 0)
		error("bad 9P version returned from server");
	if(returnlen > 0 && returnlen < k)
		error(Eshort);

	v = nil;
	kstrdup(&v, f.version);
	q = qopen(10*MAXRPC, 0, nil, nil);
	if(q == nil){
		free(v);
		exhausted("mount queues");
	}

	/* now build Mnt associated with this connection */
	lock(&mntalloc);
	mnt = mntalloc.mntfree;
	if(mnt != nil)
		mntalloc.mntfree = mnt->list;
	else {
		unlock(&mntalloc);
		mnt = malloc(sizeof(Mnt));
		if(mnt == nil) {
			qfree(q);
			free(v);
			exhausted("mount devices");
		}
		lock(&mntalloc);
	}
	mnt->list = mntalloc.list;
	mntalloc.list = mnt;
	mnt->version = v;
	mnt->id = mntalloc.id++;
	mnt->q = q;
	mnt->msize = f.msize;
	unlock(&mntalloc);

	if(returnlen > 0)
		memmove(version, f.version, k);	/* length was checked above */

	poperror();	/* msg */
	free(msg);

	lock(&mnt->l);
	mnt->queue = nil;
	mnt->rip = nil;

	c->flag |= CMSG;
	c->mux = mnt;
	mnt->c = c;
	unlock(&mnt->l);

	poperror();	/* c */
	qunlock(&c->umqlock);

	return k;
}

Chan*
mntauth(Chan *c, char *spec)
{
	Mnt *mnt;
	Mntrpc *r;

	mnt = c->mux;
	if(mnt == nil){
		mntversion(c, 0, nil, 0);
		mnt = c->mux;
		if(mnt == nil)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tauth;
	r->request.afid = c->fid;
	r->request.uname = up->user;
	r->request.aname = spec;
	mountrpc(mnt, r);

	c->qid = r->reply.aqid;
	c->mchan = mnt->c;
	incref(&mnt->c->r);
	c->mqid = c->qid;
	c->mode = ORDWR;
	c->iounit = mnt->msize-IOHDRSZ;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	return c;

}

Chan*
mntattach(Chan *c, Chan *ac, char *spec, int flags)
{
	Mnt *mnt;
	Mntrpc *r;

	mnt = c->mux;
	if(mnt == nil){
		mntversion(c, 0, nil, 0);
		mnt = c->mux;
		if(mnt == nil)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tattach;
	r->request.fid = c->fid;
	if(ac == nil)
		r->request.afid = NOFID;
	else
		r->request.afid = ac->fid;
	r->request.uname = up->user;
	r->request.aname = spec;
	mountrpc(mnt, r);

	c->qid = r->reply.qid;
	c->mchan = mnt->c;
	incref(&mnt->c->r);
	c->mqid = c->qid;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	return c;
}
#if 0
static Chan*
noattach(Chan *c, Chan *ac, char *spec, int flags)
{
	error(Enoattach);
	return nil;
}
#endif

static Chan*
mntchan(void)
{
	Chan *c;

	c = devattach('9', 0);
	lock(&mntalloc);
	c->devno = mntalloc.id++;
	unlock(&mntalloc);

	if(c->mchan != nil)
		panic("mntchan non-zero %#p", c->mchan);
	return c;
}

static Walkqid*
mntwalk(Chan *c, Chan *nc, char **name, int nname)
{
	int i, alloc;
	Mnt *mnt;
	Mntrpc *r;
	Walkqid *wq;

	if(nc != nil)
		print("mntwalk: nc != nil\n");
	if(nname > MAXWELEM)
		error("devmnt: too many name elements");
	alloc = 0;
	wq = smalloc(sizeof(Walkqid)+(nname-1)*sizeof(Qid));
	if(waserror()){
		if(alloc && wq->clone!=nil)
			cclose(wq->clone);
		free(wq);
		return nil;
	}

	alloc = 0;
	mnt = mntchk(c);
	r = mntralloc(c);
	if(nc == nil){
		nc = devclone(c);
		/*
		 * Until the other side accepts this fid, we can't mntclose it.
		 * Therefore set type to 0 for now; rootclose is known to be safe.
		 */
		nc->dev = nil;
		alloc = 1;
	}
	wq->clone = nc;

	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twalk;
	r->request.fid = c->fid;
	r->request.newfid = nc->fid;
	r->request.nwname = nname;
	memmove(r->request.wname, name, nname*sizeof(char*));

	mountrpc(mnt, r);

	if(r->reply.nwqid > nname)
		error("too many QIDs returned by walk");
	if(r->reply.nwqid < nname){
		if(alloc)
			cclose(nc);
		wq->clone = nil;
		if(r->reply.nwqid == 0){
			free(wq);
			wq = nil;
			goto Return;
		}
	}

	/* move new fid onto mnt device and update its qid */
	if(wq->clone != nil){
		if(wq->clone != c){
			wq->clone->dev = c->dev;
			wq->clone->mchan = c->mchan;
			incref(&c->mchan->r);
		}
		if(r->reply.nwqid > 0)
			wq->clone->qid = r->reply.wqid[r->reply.nwqid-1];
	}
	wq->nqid = r->reply.nwqid;
	for(i=0; i<wq->nqid; i++)
		wq->qid[i] = r->reply.wqid[i];

    Return:
	poperror();
	mntfree(r);
	poperror();
	return wq;
}

static long
mntstat(Chan *c, uint8_t *dp, long n)
{
	Mnt *mnt;
	Mntrpc *r;

	if(n < BIT16SZ)
		error(Eshortstat);
	mnt = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tstat;
	r->request.fid = c->fid;
	mountrpc(mnt, r);

	if(r->reply.nstat > n){
		n = BIT16SZ;
		PBIT16(dp, r->reply.nstat-2);
	}else{
		n = r->reply.nstat;
		memmove(dp, r->reply.stat, n);
		validstat(dp, n);
		mntdirfix(dp, c);
	}
	poperror();
	mntfree(r);
	return n;
}

static unsigned long
ninep2mode(int omode9p)
{
	unsigned long mode = 0;

	/* 9P2000 allows a single byte for open mode and all the
	 * constants are hardcoded in the protocol
	 * see http://man.cat-v.org/9front/5/open
	 */
	if((omode9p&~0xff) || (omode9p&NP_OZEROES))
		error("invalid 9P2000 open mode");

	switch(omode9p & ~(NP_OTRUNC|NP_ORCLOSE|NP_OCEXEC)){
	case NP_OREAD:
		return OREAD;
	case NP_OWRITE:
		mode = OWRITE;
		break;
	case NP_ORDWR:
		mode = ORDWR;
		break;
	case NP_OEXEC:
		mode = OEXEC;
		break;
	default:
		error("invalid 9P2000 open mode");
	}
	if(omode9p & NP_OCEXEC)
		mode |= OCEXEC;
	if(omode9p & NP_ORCLOSE)
		mode |= ORCLOSE;
	if(omode9p & NP_OTRUNC)
		mode |= OTRUNC;

	return mode;
}

static int
mode2ninep(unsigned long mode)
{
	int omode9p = 0;

	openmode(mode); /* error check only */

	/* 9P2000 allows a single byte for open mode and all the
	 * constants are hardcoded in the protocol
	 * see http://man.cat-v.org/9front/5/open
	 */
	if((mode&OKMODE) == OREAD)
		return NP_OREAD;
	if((mode&ORDWR) == ORDWR)
		omode9p |= NP_ORDWR;
	else if(mode & OWRITE)
		omode9p |= NP_OWRITE;
	else if(mode & OEXEC)
		omode9p |= NP_OEXEC;
	if(mode & ORCLOSE)
		omode9p |= NP_ORCLOSE;
	if(mode & OCEXEC)
		omode9p |= NP_OCEXEC;

	/* this is an approssimation: in Jehanne this bit might means
	 * something different to a server, but then the
	 * server should not use 9P2000
	 */
	if(mode & OTRUNC)
		omode9p |= NP_OTRUNC;

	return omode9p;
}

static Chan*
mntopencreate(int type, Chan *c, char *name, unsigned long omode, int perm)
{
	Mnt *mnt;
	Mntrpc *r;

	mnt = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = type;
	r->request.fid = c->fid;
	r->request.mode = mode2ninep(omode);
	if(type == Tcreate){
		r->request.perm = perm;
		r->request.name = name;
	}
	mountrpc(mnt, r);

	c->qid = r->reply.qid;
	c->offset = 0;
	c->mode = ninep2mode(mode2ninep(omode)); // discards unsopported flags
	c->iounit = r->reply.iounit;
	if(c->iounit == 0 || c->iounit > mnt->msize-IOHDRSZ)
		c->iounit = mnt->msize-IOHDRSZ;
	c->flag |= COPEN;
	poperror();
	mntfree(r);

	return c;
}

static Chan*
mntopen(Chan *c, unsigned long omode)
{
	return mntopencreate(Topen, c, nil, omode, 0);
}

static Chan*
mntcreate(Chan *c, char *name, unsigned long omode, unsigned long perm)
{
	return mntopencreate(Tcreate, c, name, omode, perm);
}

static void
mntclunk(Chan *c, int t)
{
	Mnt *mnt;
	Mntrpc *r;


	mnt = mntchk(c);
	r = mntralloc(c);
	if(waserror()){
		mntfree(r);
		nexterror();
	}
	r->request.type = t;
	r->request.fid = c->fid;
	mountrpc(mnt, r);
	mntfree(r);
	poperror();
}

void
muxclose(Mnt *mnt)
{
	Mnt *f, **l;
	Mntrpc *r;

	while((r = mnt->queue) != nil){
		mnt->queue = r->list;
		mntfree(r);
	}
	mnt->id = 0;
	free(mnt->version);
	mnt->version = nil;
	qfree(mnt->q);
	mnt->q = nil;

	lock(&mntalloc);
	l = &mntalloc.list;
	for(f = *l; f != nil; f = f->list) {
		if(f == mnt) {
			*l = mnt->list;
			break;
		}
		l = &f->list;
	}
	mnt->list = mntalloc.mntfree;
	mntalloc.mntfree = mnt;
	unlock(&mntalloc);
}

static void
mntclose(Chan *c)
{
	mntclunk(c, Tclunk);
}

static void
mntremove(Chan *c)
{
	mntclunk(c, Tremove);
}

static long
mntwstat(Chan *c, uint8_t *dp, long n)
{
	Mnt *mnt;
	Mntrpc *r;

	mnt = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twstat;
	r->request.fid = c->fid;
	r->request.nstat = n;
	r->request.stat = dp;
	mountrpc(mnt, r);
	poperror();
	mntfree(r);

	return n;
}

static long
mntread(Chan *c, void *buf, long n, int64_t off)
{
	uint8_t *p, *e;
	int dirlen;

	p = buf;
	n = mntrdwr(Tread, c, p, n, off);
	if(c->qid.type & QTDIR) {
		for(e = &p[n]; p+BIT16SZ < e; p += dirlen){
			dirlen = BIT16SZ+GBIT16(p);
			if(p+dirlen > e)
				break;
			validstat(p, dirlen);
			mntdirfix(p, c);
		}
		if(p != e)
			error(Esbadstat);
	}
	return n;
}

static long
mntwrite(Chan *c, void *buf, long n, int64_t off)
{
	return mntrdwr(Twrite, c, buf, n, off);
}

long
mntrdwr(int type, Chan *c, void *buf, long n, int64_t off)
{
	Mnt *mnt;
 	Mntrpc *r;
	char *uba;
	uint32_t cnt, nr, nreq;

	mnt = mntchk(c);
	uba = buf;
	cnt = 0;

	for(;;) {
		nreq = n;
		if(nreq > c->iounit)
			nreq = c->iounit;

		r = mntralloc(c);
		if(waserror()) {
			mntfree(r);
			nexterror();
		}
		r->request.type = type;
		r->request.fid = c->fid;
		r->request.offset = off;
		r->request.data = uba;
		r->request.count = nreq;
		mountrpc(mnt, r);
		nr = r->reply.count;
		if(type == Tread){
			/* in Jehanne, we let the server respond
			 * to Twrite with a count bigger than the
			 * requested
			 */
			if(nr > nreq)
				nr = nreq;
			nr = readblist(r->b, (uint8_t*)uba, nr, 0);
		}
		mntfree(r);
		poperror();

		off += nr;
		uba += nr;
		cnt += nr;
		n -= nr;
		if(nr != nreq || n == 0 || up->nnote)
			break;
	}
	return cnt;
}

void
mountrpc(Mnt *mnt, Mntrpc *r)
{
	char *sn, *cn;
	int t;

	r->reply.tag = 0;
	r->reply.type = Tmax;	/* can't ever be a valid message type */

	mountio(mnt, r);

	t = r->reply.type;
	switch(t) {
	case Rerror:
		error(r->reply.ename);
	case Rflush:
		error(Eintr);
	default:
		if(t == r->request.type+1)
			break;
		sn = "?";
		if(mnt->c->path != nil)
			sn = mnt->c->path->s;
		cn = "?";
		if(r->c != nil && r->c->path != nil)
			cn = r->c->path->s;
		print("mnt: proc %s %d: mismatch from %s %s rep %#p tag %d fid %d T%d R%d rp %d\n",
			up->text, up->pid, sn, cn,
			r, r->request.tag, r->request.fid, r->request.type,
			r->reply.type, r->reply.tag);
		error(Emountrpc);
	}
}

void
mountio(Mnt *mnt, Mntrpc *r)
{
	Block *b;
	int n;
	Syscalls awksysc;

	awksysc = 0;
	while(waserror()) {
		if(mnt->rip == up)
			mntgate(mnt);
		if(strcmp(up->errstr, Eintr) != 0 || waserror()){
			r = mntflushfree(mnt, r);
			switch(r->request.type){
			default:
				break;
			case Tremove:
			case Tclunk:
				/* botch, abandon fid */
				if(strcmp(up->errstr, Ehungup) != 0)
					r->c->fid = 0;
			}
			nexterror();
		}
		r = mntflushalloc(r);
		poperror();
	}

	lock(&mnt->l);
	r->z = &up->sleep;
	r->mnt = mnt;
	r->list = mnt->queue;
	mnt->queue = r;
	unlock(&mnt->l);

	/* Transmit a file system rpc */
	n = sizeS2M(&r->request);
	b = allocb(n);
	if(waserror()){
		freeb(b);
		nexterror();
	}
	n = convS2M(&r->request, b->wp, n);
	if(n <= 0 || n > mnt->msize) {
		print("mountio: proc %s %lud: convS2M returned %d for tag %d fid %d T%d\n",
			up->text, up->pid, n, r->request.tag, r->request.fid, r->request.type);
		error(Emountrpc);
	}
	b->wp += n;
	poperror();

	if(r->request.type == Tflush){
		/* Tflush must not be interrupted by awake.
		 *
		 * The following code might call sleep() but it must
		 * since the syscall has already been interrupted
		 * (or we would not have to send a Tflush) awake must
		 * not interrupt such sleep.
		 *
		 * Thus we disable awake otherwise we will consume all
		 * tags trying to flush the flush.
		 *
		 * TODO: verify we do not have to do the same
		 * with notes
		 */
		awksysc = awake_disable();
	}
	mnt->c->dev->bwrite(mnt->c, b, 0);

	/* Gate readers onto the mount point one at a time */
	for(;;) {
		lock(&mnt->l);
		if(mnt->rip == nil)
			break;
		unlock(&mnt->l);
		sleep(r->z, rpcattn, r);
		if(r->done){
			if(r->request.type == Tflush)
				awake_enable(awksysc);
			poperror();
			mntflushfree(mnt, r);
			return;
		}
	}
	mnt->rip = up;
	unlock(&mnt->l);
	while(r->done == 0) {
		if(mntrpcread(mnt, r) < 0)
			error(Emountrpc);
		mountmux(mnt, r);
	}
	if(r->request.type == Tflush)
		awake_enable(awksysc);

	mntgate(mnt);
	poperror();
	mntflushfree(mnt, r);
}

static int
doread(Mnt *mnt, int len)
{
	Block *b;

	while(qlen(mnt->q) < len){
		b = mnt->c->dev->bread(mnt->c, mnt->msize, 0);
		if(b == nil || qaddlist(mnt->q, b) == 0)
			return -1;
	}
	return 0;
}

static int
mntrpcread(Mnt *mnt, Mntrpc *r)
{
	int i, t, len, hlen;
	Block *b, **l, *nb;

	r->reply.type = 0;
	r->reply.tag = 0;

	/* read at least length, type, and tag and pullup to a single block */
	if(doread(mnt, BIT32SZ+BIT8SZ+BIT16SZ) < 0)
		return -1;
	nb = pullupqueue(mnt->q, BIT32SZ+BIT8SZ+BIT16SZ);

	/* read in the rest of the message, avoid ridiculous (for now) message sizes */
	len = GBIT32(nb->rp);
	if(len > mnt->msize){
		qdiscard(mnt->q, qlen(mnt->q));
		return -1;
	}
	if(doread(mnt, len) < 0)
		return -1;

	/* pullup the header (i.e. everything except data) */
	t = nb->rp[BIT32SZ];
	switch(t){
	case Rread:
		hlen = BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ;
		break;
	default:
		hlen = len;
		break;
	}
	nb = pullupqueue(mnt->q, hlen);

	if(convM2S(nb->rp, len, &r->reply) <= 0){
		/* bad message, dump it */
		print("mntrpcread: convM2S failed\n");
		qdiscard(mnt->q, len);
		return -1;
	}

	/* hang the data off of the fcall struct */
	l = &r->b;
	*l = nil;
	do {
		b = qremove(mnt->q);
		if(hlen > 0){
			b->rp += hlen;
			len -= hlen;
			hlen = 0;
		}
		i = BLEN(b);
		if(i <= len){
			len -= i;
			*l = b;
			l = &(b->next);
		} else {
			/* split block and put unused bit back */
			nb = allocb(i-len);
			memmove(nb->wp, b->rp+len, i-len);
			b->wp = b->rp+len;
			nb->wp += i-len;
			qputback(mnt->q, nb);
			*l = b;
			return 0;
		}
	}while(len > 0);

	return 0;
}

void
mntgate(Mnt *mnt)
{
	Mntrpc *q;

	lock(&mnt->l);
	mnt->rip = nil;
	for(q = mnt->queue; q != nil; q = q->list) {
		if(q->done == 0)
		if(wakeup(q->z))
			break;
	}
	unlock(&mnt->l);
}

void
mountmux(Mnt *mnt, Mntrpc *r)
{
	Mntrpc **l, *q;
	Rendez *z;

	lock(&mnt->l);
	l = &mnt->queue;
	for(q = *l; q; q = q->list) {
		/* look for a reply to a message */
		if(q->request.tag == r->reply.tag) {
			*l = q->list;
			if(q == r) {
				q->done = 1;
				unlock(&mnt->l);
				return;
			}
			/*
			 * Completed someone else.
			 * Trade pointers to receive buffer.
			 */
			q->reply = r->reply;
			q->b = r->b;
			r->b = nil;
			z = q->z;
			coherence();
			q->done = 1;
			wakeup(z);
			unlock(&mnt->l);
			return;
		}
		l = &q->list;
	}
	unlock(&mnt->l);
	print("unexpected reply tag %ud; type %d\n", r->reply.tag, r->reply.type);
}

/*
 * Create a new flush request and chain the previous
 * requests from it
 */
static Mntrpc*
mntflushalloc(Mntrpc *r)
{
	Mntrpc *fr;

	fr = mntralloc(r->c);
	fr->request.type = Tflush;
	if(r->request.type == Tflush)
		fr->request.oldtag = r->request.oldtag;
	else
		fr->request.oldtag = r->request.tag;
	fr->flushed = r;

	return fr;
}

/*
 *  Free a chain of flushes.  Remove each unanswered
 *  flush and the original message from the unanswered
 *  request queue.  Mark the original message as done
 *  and if it hasn't been answered set the reply to to
 *  Rflush. Return the original rpc.
 */
static Mntrpc*
mntflushfree(Mnt *mnt, Mntrpc *r)
{
	Mntrpc *fr;

	while(r != nil){
		fr = r->flushed;
		if(!r->done){
			r->reply.type = Rflush;
			mntqrm(mnt, r);
		}
		if(fr == nil)
			break;
		mntfree(r);
		r = fr;
	}
	return r;
}

int
alloctag(void)
{
	int i, j;
	uint32_t v;

	for(i = 0; i < NMASK; i++){
		v = mntalloc.tagmask[i];
		if(v == (uint32_t)~0UL)
			continue;
		for(j = 0; j < 1<<TAGSHIFT; j++)
			if((v & (1<<j)) == 0){
				mntalloc.tagmask[i] |= 1<<j;
				return (i<<TAGSHIFT) + j;
			}
	}
	panic("no friggin tags left");
	return NOTAG;
}

static void
freetag(int t)
{
	mntalloc.tagmask[t>>TAGSHIFT] &= ~(1<<(t&TAGMASK));
}

static Mntrpc*
mntralloc(Chan *c)
{
	Mntrpc *new;

	if(mntalloc.nrpcfree == 0) {
	Alloc:
		new = malloc(sizeof(Mntrpc));
		if(new == nil)
			exhausted("mount rpc header");
		lock(&mntalloc);
		new->request.tag = alloctag();
	} else {
		lock(&mntalloc);
		new = mntalloc.rpcfree;
		if(new == nil) {
			unlock(&mntalloc);
			goto Alloc;
		}
		mntalloc.rpcfree = new->list;
		mntalloc.nrpcfree--;
	}
	mntalloc.nrpcused++;
	unlock(&mntalloc);
	new->c = c;
	new->done = 0;
	new->flushed = nil;
	new->b = nil;
	new->w = nil;
	return new;
}

static void
mntfree(Mntrpc *r)
{
	freeb(r->w);
	freeblist(r->b);
	lock(&mntalloc);
	mntalloc.nrpcused--;
	if(mntalloc.nrpcfree < 32) {
		r->list = mntalloc.rpcfree;
		mntalloc.rpcfree = r;
		mntalloc.nrpcfree++;
		unlock(&mntalloc);
		return;
	}
	freetag(r->request.tag);
	unlock(&mntalloc);
	free(r);
}

void
mntqrm(Mnt *mnt, Mntrpc *r)
{
	Mntrpc **l, *f;

	lock(&mnt->l);
	r->done = 1;

	l = &mnt->queue;
	for(f = *l; f != nil; f = f->list) {
		if(f == r) {
			*l = r->list;
			break;
		}
		l = &f->list;
	}
	unlock(&mnt->l);
}

Mnt*
mntchk(Chan *c)
{
	Mnt *mnt;

	/* This routine is mostly vestiges of prior lives; now it's just sanity checking */
	if(c->mchan == nil)
		panic("mntchk 1: nil mchan c %s\n", chanpath(c));

	mnt = c->mchan->mux;
	if(mnt == nil)
		print("mntchk 2: nil mux c %s c->mchan %s \n", chanpath(c), chanpath(c->mchan));

	/*
	 * Was it closed and reused (was error(Eshutdown); now, it cannot happen)
	 */
	if(mnt->id == 0 || mnt->id >= c->devno)
		panic("mntchk 3: can't happen");

	return mnt;
}

/*
 * Rewrite channel type and dev for in-flight data to
 * reflect local values.  These entries are known to be
 * the first two in the Dir encoding after the count.
 */
static void
mntdirfix(uint8_t *dirbuf, Chan *c)
{
	uint32_t r;

	r = c->dev->dc;
	dirbuf += BIT16SZ;	/* skip count */
	PBIT16(dirbuf, r);
	dirbuf += BIT16SZ;
	PBIT32(dirbuf, c->devno);
}

int
rpcattn(void *v)
{
	Mntrpc *r;

	r = v;
	return r->done || r->mnt->rip == nil;
}

Dev ninepdevtab = {
	'9',
	"ninep",

	mntreset,
	devinit,
	devshutdown,
	mntattach,
	mntwalk,
	mntstat,
	mntopen,
	mntcreate,
	mntclose,
	mntread,
	devbread,
	mntwrite,
	devbwrite,
	mntremove,
	mntwstat,
};
