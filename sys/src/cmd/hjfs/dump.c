#include <u.h>
#include <lib9.h>
#include <thread.h>
#include <9P2000.h>
#include "dat.h"
#include "fns.h"

int
copydentry(Fs *fs, FLoc *a, Loc *b, char *nname)
{
	Buf *ba, *bb, *bc;
	Dentry *d;
	int i, rc;
	FLoc c;

	if(!namevalid(nname)){
		werrstr(Einval);
		return -1;
	}
	ba = getbuf(fs->d, a->blk, TDENTRY, 0);
	if(ba == nil)
		return -1;
	bb = getbuf(fs->d, b->blk, TDENTRY, 0);
	if(bb == nil){
		putbuf(ba);
		return -1;
	}
	rc = newentry(fs, b, bb, nname, &c, 1);
	if(rc < 0){
	err1:
		putbuf(bb);
		putbuf(ba);
		return -1;
	}
	bc = getbuf(fs->d, c.blk, TDENTRY, 0);
	if(bc == nil)
		goto err1;
	d = &bc->de[c.deind];
	memcpy(d, &ba->de[a->deind], sizeof(*d));
	strcpy(d->name, nname);
	for(i = 0; i < NDIRECT; i++)
		if(d->db[i] != 0)
			chref(fs, d->db[i], 1);
	for(i = 0; i < NINDIRECT; i++)
		if(d->ib[i] != 0)
			chref(fs, d->ib[i], 1);
	bc->op |= BDELWRI;
	putbuf(bc);
	putbuf(bb);
	putbuf(ba);
	return 0;
}

static void
resetldumped(Fs *fs)
{
	Loc *l;
	
	for(l = fs->rootloc->gnext; l != fs->rootloc; l = l->gnext)
		l->flags &= ~LDUMPED;
}

int
fsdump(Fs *fs)
{
	char buf[20], *p, *e;
	int n, rc;
	Tm *tm;
	Chan *ch, *chh;
	Buf *b;

	wlock(fs);
	tm = localtime(time(0));
	snprint(buf, sizeof(buf), "%.4d", tm->year + 1900);
	ch = chanattach(fs, CHFNOLOCK|CHFDUMP);
	ch->uid = -1;
	if(ch == nil){
		wunlock(fs);
		return -1;
	}
	if(chanwalk(ch, buf) < 0){
		chh = chanclone(ch);
		rc = chancreat(chh, buf, DMDIR|0555, NP_OREAD);
		chanclunk(chh);
		if(rc < 0)
			goto err;
		if(chanwalk(ch, buf) < 0)
			goto err;
	}
	b = getbuf(fs->d, ch->loc->blk, TDENTRY, 0);
	if(b == nil)
		goto err;
	for(n = 0; ; n++){
		e = buf + sizeof(buf);
		p = seprint(buf, e, "%.2d%.2d", tm->mon + 1, tm->mday);
		if(n > 0)
			seprint(p, e, "%d", n);
		rc = findentry(fs, ch->loc, b, buf, nil, 1);
		if(rc < 0)
			goto err;
		if(rc == 0)
			break;
	}
	putbuf(b);
	rc = copydentry(fs, fs->rootloc, ch->loc, buf);
	chanclunk(ch);
	resetldumped(fs);
	wunlock(fs);
	return rc;
err:
	chanclunk(ch);
	wunlock(fs);
	return -1;
}

int
willmodify(Fs *fs, Loc *l, int nolock)
{
	Buf *p;
	Loc *m;
	uint64_t i, r;
	Dentry *d;
	int rc;

	if((l->flags & LDUMPED) != 0)
		return 1;
	if(!nolock){
again:
		runlock(fs);
		wlock(fs);
	}
	if(l->next != nil && willmodify(fs, l->next, 1) < 0)
		goto err;
	rc = chref(fs, l->blk, 0);
	if(rc < 0)
		goto err;
	if(rc == 0){
		dprint("hjfs: willmodify: block %lld has refcount 0\n", l->blk);
		werrstr("phase error -- willmodify");
		goto err;
	}
	if(rc == 1)
		goto done;

	p = getbuf(fs->d, l->next->blk, TDENTRY, 0);
	if(p == nil)
		goto err;
	d = getdent(l->next, p);
	if(d != nil) for(i = 0; i < d->size; i++){
		rc = getblk(fs, l->next, p, i, &r, GBREAD);
		if(rc <= 0)
			continue;
		if(r == l->blk)
			goto found;
	}	
phase:
	werrstr("willmodify -- phase error");
	putbuf(p);
	goto err;
found:
	rc = getblk(fs, l->next, p, i, &r, GBWRITE);
	if(rc < 0){
		putbuf(p);
		goto err;
	}
	if(rc == 0)
		goto phase;
	putbuf(p);

	if(r != l->blk){
		/*
		 * block got dumped, update the loctree so locs
		 * point to the new block.
		 */
		qlock(&fs->loctree);
		for(m = l->cnext; m != l; m = m->cnext)
			if(m->blk == l->blk)
				m->blk = r;
		l->blk = r;
		qunlock(&fs->loctree);
	}
done:
	l->flags |= LDUMPED;
	if(!nolock){
		wunlock(fs);
		rlock(fs);
		if(chref(fs, l->blk, 0) != 1)
			goto again;
	}
	return 0;
err:
	if(!nolock){
		wunlock(fs);
		rlock(fs);
	}
	return -1;
}
