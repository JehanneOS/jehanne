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
#include "dat.h"
#include "fns.h"

int
chref(Fs *fs, uint64_t r, int stat)
{
	uint64_t i;
	int j;
	uint32_t rc;
	Buf *c;

	i = fs->fstart + r / REFPERBLK;
	j = r % REFPERBLK;
	c = getbuf(fs->d, i, TREF, 0);
	if(c == nil)
		return -1;
	if(stat < 0 && c->refs[j] < -stat)
		c->refs[j] = 0;
	else
		c->refs[j] += stat;
	rc = c->refs[j];
	if(stat != 0)
		c->op |= BDELWRI;
	putbuf(c);
	return rc;
}

static int
qidcmp(Qid *a, Qid *b)
{
	if(a->type != b->type)
		return 1;
	if(a->path != b->path){
		/* special case for dump, this is ok */
		if(a->path==ROOTQID && b->path==DUMPROOTQID)
			return 0;
		return 1;
	}
	return 0;
}

Dentry*
getdent(FLoc *l, Buf *b)
{
	Dentry *d;

	d = &b->de[l->deind];
	if((d->mode & (DGONE | DALLOC)) == 0){
		dprint("getdent: file gone, d=%llux, l=%llud/%d %llux, callerpc %#p\n",
			d->path, l->blk, l->deind, l->path, getcallerpc());
		werrstr("phase error -- directory entry for nonexistent file");
		return nil;
	}
	if(qidcmp(d, l) != 0){
		dprint("getdent: wrong qid d=%llux != l=%llud/%d %llux, callerpc %#p\n",
			d->path, l->blk, l->deind, l->path, getcallerpc());
		werrstr("phase error -- qid mismatch");
		return nil;
	}
	return d;
}

int
getfree(Fs *fs, uint64_t *r)
{
	Dev *d;
	Buf *b;
	uint64_t i, l, e;
	int j, have;

	d = fs->d;
	while(nbrecv(fs->freelist, &l) > 0){
		i = fs->fstart + l / REFPERBLK;
		j = l % REFPERBLK;
		b = getbuf(d, i, TREF, 0);
		if(b != nil){
			if(b->refs[j] == 0){
				b->refs[j] = 1;
				*r = l;
				goto found;
			}
			putbuf(b);
		}
	}

	b = getbuf(d, SUPERBLK, TSUPERBLOCK, 0);
	if(b == nil) {
		werrstr("could not find superblock");
		return -1;
	}
	e = b->sb.fend;
	putbuf(b);

	have = 0;
	for(l = 0, i = fs->fstart; i < e; i++){
		b = getbuf(d, i, TREF, 0);
		if(b == nil){
			l += REFPERBLK;
			continue;
		}
		for(j = 0; j < REFPERBLK; j++, l++)
			if(b->refs[j] == 0){
				if(!have){
					b->refs[j] = 1;
					*r = l;
					have = 1;
				}else if(nbsend(fs->freelist, &l) <= 0)
					goto found;
			}
		if(have)
			goto found;
		putbuf(b);
	}
	werrstr("disk full");
	return -1;
found:
	b->op |= BDELWRI;
	putbuf(b);
	return 1;
}

int
putfree(Fs *fs, uint64_t r)
{
	int rc;

	rc = chref(fs, r, -1);
	if(rc < 0)
		return -1;
	if(rc == 0)
		nbsend(fs->freelist, &r);
	return 1;
}

static void
createroot(Fs *fs)
{
	uint64_t r;
	Buf *c;
	Dentry *d;

	if(getfree(fs, &r) < 0)
		sysfatal("ream: getfree: %r");
	c = getbuf(fs->d, r, TDENTRY, 1);
	if(c == nil)
	error:
		sysfatal("ream: getbuf: %r");
	memset(c->de, 0, sizeof(c->de));
	d = &c->de[0];
	strcpy(d->name, "/");
	d->mode = DALLOC | 0775;
	d->path = ROOTQID;
	d->type = QTDIR;
	d->uid = -1;
	d->muid = -1;
	d->gid = -1;
	d->mtime = time(0);
	d->atime = d->mtime;
	d++;
	strcpy(d->name, "/");
	d->mode = DALLOC | 0775;
	d->path = ROOTQID;
	d->type = QTDIR;
	d->uid = -1;
	d->muid = -1;
	d->gid = -1;
	d->mtime = time(0);
	d->atime = d->mtime;
	c->op |= BWRIM;
	putbuf(c);
	c = getbuf(fs->d, SUPERBLK, TSUPERBLOCK, 0);
	if(c == nil)
		goto error;
	fs->root = r;
	c->sb.root = r;
	c->op |= BDELWRI;
	putbuf(c);
}

void
writeusers(Fs *fs)
{
	Chan *ch;

	ch = chanattach(fs, 0);
	if(ch == nil)
		goto error;
	ch->uid = -1;
	chancreat(ch, "cfg", DMDIR | 0775, NP_OREAD);
	chanclunk(ch);
	ch = chanattach(fs, 0);
	if(ch == nil)
		goto error;
	ch->uid = -1;
	if(chanwalk(ch, "cfg") <= 0)
		goto error;
	if(chanwalk(ch, "users") > 0){
		if(chanopen(ch, NP_OWRITE|NP_OTRUNC) <= 0)
			goto error;
	}else if(chancreat(ch, "users", 0664, NP_OWRITE) <= 0)
			goto error;
	if(userssave(fs, ch) < 0){
		chanremove(ch);
		ch = nil;
		goto error;
	}
	chanclunk(ch);
	return;
error:
	if(ch != nil)
		chanclunk(ch);
	dprint("writeusers: %r\n");
}

void
readusers(Fs *fs)
{
	Chan *ch;

	ch = chanattach(fs, 0);
	if(ch == nil)
		goto err;
	ch->uid = -1;
	if(chanwalk(ch, "cfg") <= 0)
		goto err;
	if(chanwalk(ch, "users") <= 0)
		goto err;
	if(chanopen(ch, NP_OREAD) < 0)
		goto err;
	if(usersload(fs, ch) < 0)
		goto err;
	chanclunk(ch);
	return;
err:
	if(ch != nil)
		chanclunk(ch);
	dprint("readusers: %r\nhjfs: using default user db\n");
}

void
ream(Fs *fs)
{
	Dev *d;
	Buf *b, *c;
	uint64_t i, firsti, lasti;
	int j, je;

	d = fs->d;
	dprint("reaming %s\n", d->name);
	b = getbuf(d, SUPERBLK, TSUPERBLOCK, 1);
	if(b == nil)
	err:
		sysfatal("ream: getbuf: %r");
	memset(&b->sb, 0, sizeof(b->sb));
	b->sb.magic = SUPERMAGIC;
	b->sb.size = d->size;
	b->sb.fstart = SUPERBLK + 1;
	fs->fstart = b->sb.fstart;
	b->sb.fend = b->sb.fstart + HOWMANY(b->sb.size * REFSIZ);
	b->sb.qidpath = DUMPROOTQID + 1;
	firsti = b->sb.fstart + SUPERBLK / REFPERBLK;
	lasti = b->sb.fstart + b->sb.fend / REFPERBLK;
	for(i = b->sb.fstart; i < b->sb.fend; i++){
		c = getbuf(d, i, TREF, 1);
		if(c == nil)
			goto err;
		memset(c->refs, 0, sizeof(c->refs));
		if(i >= firsti && i <= lasti){
			j = 0;
			je = REFPERBLK;
			if(i == firsti)
				j = SUPERBLK % REFPERBLK;
			if(i == lasti)
				je = b->sb.fend % REFPERBLK;
			for(; j < je; j++)
				c->refs[j] = 1;
		}
		if(i == b->sb.fend - 1){
			j = b->sb.size % REFPERBLK;
			if(j != 0)
				for(; j < REFPERBLK; j++)
					c->refs[j] = -1;
		}
		c->op |= BWRIM;
		putbuf(c);
	}
	b->op |= BDELWRI;
	putbuf(b);
	createroot(fs);
	sync(1);
	dprint("ream successful\n");
}

Fs *
initfs(Dev *d, int doream, int flags)
{
	Fs *fs;
	Buf *b;
	FLoc f;

	fs = emalloc(sizeof(*fs));
	fs->d = d;
	if(doream)
		ream(fs);
	b = getbuf(d, SUPERBLK, TSUPERBLOCK, 0);
	if(b == nil || b->sb.magic != SUPERMAGIC)
		goto error;
	fs->root = b->sb.root;
	fs->fstart = b->sb.fstart;
	fs->freelist = chancreate(sizeof(uint64_t), FREELISTLEN);
	f.blk = fs->root;
	f.deind = 0;
	f.path = ROOTQID;
	f.vers = 0;
	f.type = QTDIR;
	fs->rootloc = getloc(fs, f, nil);
	f.deind++;
	f.path = DUMPROOTQID;
	fs->dumprootloc = getloc(fs, f, nil);
	putbuf(b);
	fs->flags = flags;
	if(doream)
		writeusers(fs);
	readusers(fs);
	dprint("fs is %s\n", d->name);
	return fs;

error:
	if(b != nil)
		putbuf(b);
	free(fs);
	return nil;
}

void
chbegin(Chan *ch)
{
	if((ch->flags & CHFNOLOCK) == 0)
		rlock(ch->fs);
}

void
chend(Chan *ch)
{
	if((ch->flags & CHFNOLOCK) == 0)
		runlock(ch->fs);
}

int
newqid(Fs *fs, uint64_t *q)
{
	Buf *b;

	b = getbuf(fs->d, SUPERBLK, TSUPERBLOCK, 0);
	if(b == nil)
		return -1;
	*q = b->sb.qidpath++;
	b->op |= BDELWRI;
	putbuf(b);
	return 1;
}

Loc *
getloc(Fs *fs, FLoc f, Loc *next)
{
	Loc *l;

	qlock(&fs->loctree);
	if(next != nil && next->child != nil){
		l = next->child;
		do{
			if(l->blk == f.blk && l->deind == f.deind){
				l->ref++;
				qunlock(&fs->loctree);
				return l;
			}
			l = l->cnext;
		}while(l != next->child);
	}
	l = emalloc(sizeof(*l));
	l->ref = 1;
	l->FLoc = f;
	l->next = next;
	if(fs->rootloc != nil){
		l->gnext = fs->rootloc;
		l->gprev = l->gnext->gprev;
		l->gnext->gprev = l;
		l->gprev->gnext = l;
	}else
		l->gnext = l->gprev = l;
	l->cprev = l->cnext = l;
	if(next != nil){
		if(next->child == nil)
			next->child = l;
		else{
			l->cnext = next->child;
			l->cprev = next->child->cprev;
			l->cnext->cprev = l;
			l->cprev->cnext = l;
		}
	}
	qunlock(&fs->loctree);
	return l;
}

int
haveloc(Fs *fs, uint64_t blk, int deind, Loc *next)
{
	Loc *l;

	qlock(&fs->loctree);
	l = next->child;
	if(l != nil) do{
		if(l->blk == blk && l->deind == deind){
			qunlock(&fs->loctree);
			return 1;
		}
		l = l->cnext;
	} while(l != next->child);
	qunlock(&fs->loctree);
	return 0;
}

Loc *
cloneloc(Fs *fs, Loc *l)
{
	Loc *m;

	qlock(&fs->loctree);
	for(m = l; m != nil; m = m->next)
		m->ref++;
	qunlock(&fs->loctree);
	return l;
}

void
putloc(Fs *fs, Loc *l, int loop)
{
	Loc *m;
	Buf *b;

	qlock(&fs->loctree);
	if(!loop && --l->ref <= 0)
		goto freeit;
	while(loop && l != nil && l->ref <= 1){
freeit:
		if((l->flags & LGONE) != 0){
			/*
			 * safe to unlock here, the file is gone and
			 * we're the last reference.
			 */
			qunlock(&fs->loctree);
			b = getbuf(fs->d, l->blk, TDENTRY, 0);
			if(b != nil){
				delete(fs, l, b);
				putbuf(b);
			}
			qlock(&fs->loctree);
		}
		l->cnext->cprev = l->cprev;
		l->cprev->cnext = l->cnext;
		l->gnext->gprev = l->gprev;
		l->gprev->gnext = l->gnext;
		if(l->next != nil && l->next->child == l){
			if(l->cnext == l)
				l->next->child = nil;
			else
				l->next->child = l->cnext;
		}
		m = l->next;
		free(l);
		l = m;
	}
	for(; loop && l != nil; l = l->next)
		--l->ref;
	qunlock(&fs->loctree);
}

static int
dumpblk(Fs *fs, FLoc * _, uint64_t *l)
{
	uint64_t n;
	int i;
	Buf *b, *c;
	Dentry *d;

	b = getbuf(fs->d, *l, TDONTCARE, 0);
	if(b == nil)
		return -1;
	if(getfree(fs, &n) < 0){
	berr:
		putbuf(b);
		return -1;
	}
	c = getbuf(fs->d, n, b->type, 1);
	if(c == nil){
		putfree(fs, n);
		goto berr;
	}
	switch(b->type){
	case TINDIR:
		memcpy(c->offs, b->offs, sizeof(b->offs));
		for(i = 0; i < OFFPERBLK; i++)
			if(b->offs[i] != 0)
				chref(fs, b->offs[i], 1);
		break;
	case TRAW:
		memcpy(c->data, b->data, sizeof(b->data));
		break;
	case TDENTRY:
		memcpy(c->de, b->de, sizeof(b->de));
		for(d = b->de; d < &b->de[DEPERBLK]; d++){
			if((d->mode & DALLOC) == 0)
				continue;
			if((d->type & QTTMP) != 0)
				continue;
			for(i = 0; i < NDIRECT; i++)
				if(d->db[i] != 0)
					chref(fs, d->db[i], 1);
			for(i = 0; i < NINDIRECT; i++)
				if(d->ib[i] != 0)
					chref(fs, d->ib[i], 1);
		}
		break;
	default:
		werrstr("dumpblk -- unknown type %d", b->type);
		putfree(fs, n);
		putbuf(c);
		putbuf(b);
		return -1;
	}
	c->op |= BDELWRI;
	putbuf(c);
	putbuf(b);
	putfree(fs, *l);
	*l = n;
	return 0;
}

/*
 * getblk returns the address of a block in a file
 * given its relative address blk
 * the address are returned in *r
 * mode has to be one of:
 * - GBREAD: this block will only be read
 * - GBWRITE: this block will be written, but don't create it
 *            if it doesn't exist
 * - GBCREATE: this block will be written, create it if necessary
 * - GBOVERWR: like GBCREATE, but return an empty block if a dump
 *             would be necessary
 * return value is 1 if the block existed, -1 on error
 * a return value of 0 means the block did not exist
 * this is only an error in case of GBREAD and GBWRITE
 */

int
getblk(Fs *fs, FLoc *L, Buf *bd, uint64_t blk, uint64_t *r, int mode)
{
	uint64_t k, l;
	uint64_t *loc;
	int i, j, rc, prc;
	Buf *b;
	Dentry *d;

	b = bd;
	d = getdent(L, b);
	if(d == nil){
		dprint("getblk: dirent gone\n");
		return -1;
	}
	if(blk < NDIRECT){
		loc = &d->db[blk];
		goto found;
	}
	blk -= NDIRECT;
	l = 1;
	for(i = 0; i < NINDIRECT; i++){
		l *= OFFPERBLK;
		if(blk < l)
			break;
		blk -= l;
	}
	if(i == NINDIRECT){
		werrstr("getblk: block offset too large");
		return -1;
	}
	loc = &d->ib[i];
	for(j = 0; j <= i; j++){
		if(*loc == 0){
			if(mode == GBREAD || mode == GBWRITE){
				if(b != bd)
					putbuf(b);
				return 0;
			}
			if(getfree(fs, loc) < 0){
				if(b != bd)
					putbuf(b);
				return -1;
			}
			b->op |= BDELWRI;
			k = *loc;
			if(b != bd)
				putbuf(b);
			b = getbuf(fs->d, k, TINDIR, 1);
			if(b == nil)
				return -1;
			memset(b->offs, 0, sizeof(b->offs));
			b->op |= BDELWRI;
		}else{
			if(mode != GBREAD && chref(fs, *loc, 0) > 1){
				if(dumpblk(fs, L, loc) < 0){
					if(b != bd)
						putbuf(b);
					return -1;
				}
				b->op |= BDELWRI;
			}
			k = *loc;
			if(b != bd)
				putbuf(b);
			b = getbuf(fs->d, k, TINDIR, 0);
			if(b == nil)
				return -1;
		}
		l /= OFFPERBLK;
		loc = &b->offs[blk / l];
		blk %= l;
	}

found:
	rc = 0;
	prc = 0;
	if(*loc != 0){
		if(mode == GBREAD)
			goto okay;
		if((rc = chref(fs, *loc, 0)) > 1){
			if(mode == GBOVERWR){
				putfree(fs, *loc);
				*loc = 0;
				b->op |= BDELWRI;
				prc = 1;
				goto new;
			}
			if(dumpblk(fs, L, loc) < 0){
				rc = -1;
				goto end;
			}
			b->op |= BDELWRI;
		}
		if(rc < 0)
			goto end;
		if(rc == 0){
			dprint("getblk: block %lld has refcount 0\n");
			werrstr("phase error -- getblk");
			rc = -1;
			goto end;
		}
okay:
		*r = *loc;
		rc = 1;
	}else if(mode == GBCREATE || mode == GBOVERWR){
new:
		rc = getfree(fs, r);
		if(rc > 0){
			*loc = *r;
			b->op |= BDELWRI;
			rc = prc;
		}
	}
end:
	if(b != bd)
		putbuf(b);
	return rc;
}

static void
delindir(Fs *fs, uint64_t *l, int n)
{
	Buf *b;
	int k;

	if(*l == 0)
		return;
	if(chref(fs, *l, 0) > 1){
		*l = 0;
		return;
	}
	if(n > 0){
		b = getbuf(fs->d, *l, TINDIR, 0);
		if(b != nil){
			for(k = 0; k < OFFPERBLK; k++)
				if(b->offs[k] != 0){
					delindir(fs, &b->offs[k], n-1);
					b->op |= BDELWRI;
				}
			putbuf(b);
		}
	}
	putfree(fs, *l);
	*l = 0;
}

static int
zeroes(int *a, int i)
{
	while(i--)
		if(*a++ != 0)
			return 0;
	return 1;
}

static void
delindirpart(Fs *fs, FLoc *p, uint64_t *l, int *a, int n)
{
	Buf *b;
	int k;

	if(*l == 0)
		return;
	if(n == 0){
		putfree(fs, *l);
		*l = 0;
		return;
	}
	if(zeroes(a, n)){
		delindir(fs, l, n);
		return;
	}
	if(chref(fs, *l, 0) > 1)
		dumpblk(fs, p, l);
	b = getbuf(fs->d, *l, TINDIR, 0);
	if(b == nil)
		return;
	delindirpart(fs, p, &b->offs[*a], a + 1, n - 1);
	for(k = a[0] + 1; k < OFFPERBLK; k++)
		if(b->offs[k] != 0)
			delindir(fs, &b->offs[k], n - 1);
	b->op |= BDELWRI;
	putbuf(b);
}

/*
 * call willmodify() before and modified()
 * after calling this function
 */
int
trunc(Fs *fs, FLoc *ll, Buf *bd, uint64_t size)
{
	uint64_t blk;
	Dentry *d;
	int a[NINDIRECT];
	uint64_t l;
	int i, j;

	d = getdent(ll, bd);
	if(d == nil)
		return -1;
	if(size >= d->size)
		goto done;
	blk = HOWMANY(size);
	while(blk < NDIRECT){
		if(d->db[blk] != 0){
			putfree(fs, d->db[blk]);
			d->db[blk] = 0;
			bd->op |= BDELWRI;
		}
		blk++;
	}
	blk -= NDIRECT;
	l = 1;
	for(i = 0; i < NINDIRECT; i++){
		l *= OFFPERBLK;
		if(blk < l)
			break;
		blk -= l;
	}
	if(blk >= l){
		werrstr("phase error -- truncate");
		return -1;
	}
	if(blk == 0)
		goto rest;
	if(d->ib[i] == 0){
		i++;
		goto rest;
	}
	for(j = 0; j <= i; j++){
		l /= OFFPERBLK;
		a[j] = blk / l;
		blk %= l;
	}
	delindirpart(fs, ll, &d->ib[i], a, i + 1);
	i++;
rest:
	for(; i < NINDIRECT; i++)
		delindir(fs, &d->ib[i], i + 1);
done:
	d->size = size;
	bd->op |= BDELWRI;
	return 1;
}

/*
 * find a direntry
 * name == nil allows any entry to match
 * rl == nil allowed
 * return value 1 on success, 0 on Enoent,
 * -1 on other errors
 */
int
findentry(Fs *fs, FLoc *l, Buf *b, char *name, FLoc *rl, int dump)
{
	uint64_t i;
	int j;
	Dentry *d;
	uint64_t r;
	Buf *c;

	d = getdent(l, b);
	if(d == nil)
		return -1;
	for(i = 0; i < d->size; i++){
		if(getblk(fs, l, b, i, &r, GBREAD) <= 0)
			continue;
		c = getbuf(fs->d, r, TDENTRY, 0);
		if(c == nil)
			continue;
		for(j = 0; j < DEPERBLK; j++)
			if((c->de[j].mode & DALLOC) != 0 &&
			   (name == nil || strcmp(c->de[j].name, name) == 0)){
				if(dump && (c->de[j].type & QTTMP) != 0)
					continue;
			   	if(rl != nil){
					rl->Qid = c->de[j].Qid;
			   		rl->blk = r;
			   		rl->deind = j;
			   	}
				putbuf(c);
				return 1;
			}
		putbuf(c);
	}
	werrstr(Enoent);
	return 0;
}

void
modified(Chan *ch, Dentry *d)
{
	d->mtime = time(0);
	d->atime = d->mtime;
	d->muid = ch->uid;
	ch->loc->vers = ++d->vers;
}

typedef struct Del Del;

struct Del {
	FLoc;
	Del *next, *prev;
};

static int
deltraverse(Fs *fs, Del *p, Buf *b, Del **last)
{
	Buf *c;
	int frb;
	Dentry *d;
	uint64_t i, s, r;
	int j, rc;
	Del *dd;

	frb = b == nil;
	if(frb){
		b = getbuf(fs->d, p->blk, TDENTRY, 0);
		if(b == nil)
			return -1;
	}
	d = getdent(p, b);
	if(d == nil){
		if(frb)
			putbuf(b);
		return -1;
	}
	s = d->size;
	for(i = 0; i < s; i++){
		rc = getblk(fs, p, b, i, &r, GBREAD);
		if(rc <= 0)
			continue;
		c = getbuf(fs->d, r, TDENTRY, 0);
		if(c == nil)
			continue;
		for(j = 0; j < DEPERBLK; j++){
			d = &c->de[j];
			if((d->mode & (DALLOC|DGONE)) != 0){
				if((d->type & QTDIR) == 0){
					trunc(fs, p, b, 0);
					memset(d, 0, sizeof(*d));
					c->op |= BDELWRI;
				}else{
					dd = emalloc(sizeof(Del));
					dd->blk = i;
					dd->deind = j;
					dd->Qid = d->Qid;
					dd->prev = *last;
					(*last)->next = dd;
					*last = dd;
				}
			}
		}
		putbuf(c);
	}
	if(frb)
		putbuf(b);
	return 0;
}

int
delete(Fs *fs, FLoc *l, Buf *b)
{
	Dentry *d;
	Buf *c;
	Del *first, *last, *p, *q;

	d = getdent(l, b);
	if(d == nil)
		return -1;
	if((d->type & QTDIR) == 0){
		trunc(fs, l, b, 0);
		memset(d, 0, sizeof(*d));
		b->op |= BDELWRI;
		return 0;
	}
	first = last = emalloc(sizeof(Del));
	first->FLoc = *l;
	for(p = first; p != nil; p = p->next)
		deltraverse(fs, p, p == first ? b : nil, &last);
	for(p = last; p != nil; q = p->prev, free(p), p = q){
		if(p == first)
			c = b;
		else
			c = getbuf(fs->d, p->blk, TDENTRY, 0);
		if(c == nil)
			continue;
		d = getdent(p, c);
		if(d != nil){
			trunc(fs, p, c, 0);
			memset(d, 0, sizeof(*d));
			c->op |= BDELWRI;
		}
		if(p != first)
			putbuf(c);
	}
	return 0;
}

/*
 * newentry() looks for a free slot in the directory
 * and returns FLoc pointing to the slot. if no free
 * slot is available a new block is allocated. if
 * dump == 0, then the resulting blk from the FLoc
 * *is not* dumped, so to finally allocate the Dentry,
 * one has to call willmodify() on res before modyfing it.
 */
int
newentry(Fs *fs, Loc *l, Buf *b, char *name, FLoc *res, int dump)
{
	Dentry *d, *dd;
	uint64_t i, si, r;
	int j, sj;
	Buf *c;

	d = getdent(l, b);
	if(d == nil)
		return -1;
	si = sj = -1;
	for(i = 0; i < d->size; i++){
		if(getblk(fs, l, b, i, &r, GBREAD) <= 0)
			continue;
		c = getbuf(fs->d, r, TDENTRY, 0);
		if(c == nil)
			continue;
		for(j = 0; j < DEPERBLK; j++){
			dd = &c->de[j];
			if((dd->mode & DGONE) != 0)
				continue;
			if((dd->mode & DALLOC) != 0){
				if(strcmp(dd->name, name) == 0){
					werrstr(Eexists);
					putbuf(c);
					return 0;
				}
				continue;
			}
			if(si != -1 || haveloc(fs, r, j, l))
				continue;
			si = i;
			sj = j;
		}
		putbuf(c);
	}
	if(si == -1 && i == d->size){
		if(getblk(fs, l, b, i, &r, GBCREATE) >= 0){
			c = getbuf(fs->d, r, TDENTRY, 1);
			if(c != nil){
				si = i;
				sj = 0;
				d->size = i+1;
				b->op |= BDELWRI;
				memset(c->de, 0, sizeof(c->de));
				c->op |= BDELWRI;
				putbuf(c);
			}
		}
	}
	if(si == -1 || sj == -1){
		werrstr("phase error -- create");
		return -1;
	}
	if(getblk(fs, l, b, si, &res->blk, dump != 0 ? GBWRITE : GBREAD) <= 0)
		return -1;
	res->deind = sj;
	res->Qid = (Qid){0, 0, 0};
	return 1;
}
