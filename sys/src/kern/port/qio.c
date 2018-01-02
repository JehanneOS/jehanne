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

static unsigned int padblockcnt;
static unsigned int concatblockcnt;
static unsigned int pullupblockcnt;
static unsigned int copyblockcnt;
static unsigned int consumecnt;
static unsigned int producecnt;

#define QDEBUG	if(0)

/*
 *  IO queues
 */
typedef struct Queue	Queue;

struct Queue
{
	Lock;

	Block*	bfirst;		/* buffer */
	Block*	blast;

	int	len;		/* bytes allocated to queue */
	int	dlen;		/* data bytes in queue */
	int	limit;		/* max bytes in queue */
	int	inilim;		/* initial limit */
	int	state;
	int	noblock;	/* true if writes return immediately when q full */
	int	eof;		/* number of eofs read by user */

	void	(*kick)(void*);	/* restart output */
	void	(*bypass)(void*, Block*);	/* bypass queue altogether */
	void*	arg;		/* argument to kick */

	QLock	rlock;		/* mutex for reading processes */
	Rendez	rr;		/* process waiting to read */
	QLock	wlock;		/* mutex for writing processes */
	Rendez	wr;		/* process waiting to write */

	char	err[ERRMAX];
};

enum
{
	Maxatomic	= 64*1024,
};

uint32_t	qiomaxatomic = Maxatomic;

/*
 *  free a list of blocks
 */
void
freeblist(Block *b)
{
	Block *next;

	for(; b != nil; b = next){
		next = b->next;
		b->next = nil;
		freeb(b);
	}
}

/*
 *  pad a block to the front (or the back if size is negative)
 */
Block*
padblock(Block *bp, int size)
{
	int n;
	Block *nbp;

	QDEBUG checkb(bp, "padblock 0");
	if(size >= 0){
		if(bp->rp - bp->base >= size){
			bp->rp -= size;
			return bp;
		}
		n = BLEN(bp);
		nbp = allocb(size+n);
		nbp->rp += size;
		nbp->wp = nbp->rp;
		jehanne_memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		nbp->rp -= size;
	} else {
		size = -size;
		if(bp->lim - bp->wp >= size)
			return bp;
		n = BLEN(bp);
		nbp = allocb(n+size);
		jehanne_memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
	}
	nbp->next = bp->next;
	freeb(bp);
	padblockcnt++;
	QDEBUG checkb(nbp, "padblock 1");
	return nbp;
}

/*
 *  return count of bytes in a string of blocks
 */
int
blocklen(Block *bp)
{
	int len;

	len = 0;
	while(bp != nil) {
		len += BLEN(bp);
		bp = bp->next;
	}
	return len;
}

/*
 * return count of space in blocks
 */
int
blockalloclen(Block *bp)
{
	int len;

	len = 0;
	while(bp != nil) {
		len += BALLOC(bp);
		bp = bp->next;
	}
	return len;
}

/*
 *  copy the string of blocks into
 *  a single block and free the string
 */
Block*
concatblock(Block *bp)
{
	int len;

	if(bp->next == nil)
		return bp;
	len = blocklen(bp);
	concatblockcnt += len;
	return pullupblock(bp, len);
}

/*
 *  make sure the first block has at least n bytes
 */
Block*
pullupblock(Block *bp, int n)
{
	Block *nbp;
	int i;

	/*
	 *  this should almost always be true, it's
	 *  just to avoid every caller checking.
	 */
	if(BLEN(bp) >= n)
		return bp;

	/*
	 *  if not enough room in the first block,
	 *  add another to the front of the list.
	 */
	if(bp->lim - bp->rp < n){
		nbp = allocb(n);
		nbp->next = bp;
		bp = nbp;
	}

	/*
	 *  copy bytes from the trailing blocks into the first
	 */
	n -= BLEN(bp);
	while((nbp = bp->next) != nil){
		pullupblockcnt++;
		i = BLEN(nbp);
		if(i > n) {
			jehanne_memmove(bp->wp, nbp->rp, n);
			bp->wp += n;
			nbp->rp += n;
			QDEBUG checkb(bp, "pullupblock 1");
			return bp;
		} else {
			/* shouldn't happen but why crash if it does */
			if(i < 0){
				jehanne_print("pullup negative length packet, called from %#p\n",
					getcallerpc());
				i = 0;
			}
			jehanne_memmove(bp->wp, nbp->rp, i);
			bp->wp += i;
			bp->next = nbp->next;
			nbp->next = nil;
			freeb(nbp);
			n -= i;
			if(n == 0){
				QDEBUG checkb(bp, "pullupblock 2");
				return bp;
			}
		}
	}
	freeb(bp);
	return nil;
}

/*
 *  make sure the first block has at least n bytes
 */
Block*
pullupqueue(Queue *q, int n)
{
	Block *b;

	if(BLEN(q->bfirst) >= n)
		return q->bfirst;
	q->bfirst = pullupblock(q->bfirst, n);
	for(b = q->bfirst; b != nil && b->next != nil; b = b->next)
		;
	q->blast = b;
	return q->bfirst;
}

/*
 *  trim to len bytes starting at offset
 */
Block *
trimblock(Block *bp, int offset, int len)
{
	unsigned int l;
	Block *nb, *startb;

	QDEBUG checkb(bp, "trimblock 1");
	if(blocklen(bp) < offset+len) {
		freeblist(bp);
		return nil;
	}

	while((l = BLEN(bp)) < offset) {
		offset -= l;
		nb = bp->next;
		bp->next = nil;
		freeb(bp);
		bp = nb;
	}

	startb = bp;
	bp->rp += offset;

	while((l = BLEN(bp)) < len) {
		len -= l;
		bp = bp->next;
	}

	bp->wp -= (BLEN(bp) - len);

	if(bp->next != nil) {
		freeblist(bp->next);
		bp->next = nil;
	}

	return startb;
}

/*
 *  copy 'count' bytes into a new block
 */
Block*
copyblock(Block *bp, int count)
{
	int l;
	Block *nbp;

	QDEBUG checkb(bp, "copyblock 0");
	nbp = allocb(count);
	for(; count > 0 && bp != nil; bp = bp->next){
		l = BLEN(bp);
		if(l > count)
			l = count;
		jehanne_memmove(nbp->wp, bp->rp, l);
		nbp->wp += l;
		count -= l;
	}
	if(count > 0){
		jehanne_memset(nbp->wp, 0, count);
		nbp->wp += count;
	}
	copyblockcnt++;
	QDEBUG checkb(nbp, "copyblock 1");

	return nbp;
}

Block*
adjustblock(Block* bp, int len)
{
	int n;
	Block *nbp;

	if(len < 0){
		freeb(bp);
		return nil;
	}

	if(bp->rp+len > bp->lim){
		nbp = copyblock(bp, len);
		freeblist(bp);
		QDEBUG checkb(nbp, "adjustblock 1");

		return nbp;
	}

	n = BLEN(bp);
	if(len > n)
		jehanne_memset(bp->wp, 0, len-n);
	bp->wp = bp->rp+len;
	QDEBUG checkb(bp, "adjustblock 2");

	return bp;
}


/*
 *  throw away up to count bytes from a
 *  list of blocks.  Return count of bytes
 *  thrown away.
 */
int
pullblock(Block **bph, int count)
{
	Block *bp;
	int n, bytes;

	bytes = 0;
	if(bph == nil)
		return 0;

	while(*bph != nil && count != 0) {
		bp = *bph;
		n = BLEN(bp);
		if(count < n)
			n = count;
		bytes += n;
		count -= n;
		bp->rp += n;
		QDEBUG checkb(bp, "pullblock ");
		if(BLEN(bp) == 0) {
			*bph = bp->next;
			bp->next = nil;
			freeb(bp);
		}
	}
	return bytes;
}

/*
 *  get next block from a queue, return null if nothing there
 */
Block*
qget(Queue *q)
{
	int dowakeup;
	Block *b;

	/* sync with qwrite */
	ilock(q);

	b = q->bfirst;
	if(b == nil){
		q->state |= Qstarve;
		iunlock(q);
		return nil;
	}
	QDEBUG checkb(b, "qget");
	q->bfirst = b->next;
	b->next = nil;
	q->len -= BALLOC(b);
	q->dlen -= BLEN(b);

	/* if writer flow controlled, restart */
	if((q->state & Qflow) && q->len < q->limit/2){
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	iunlock(q);

	if(dowakeup)
		wakeup(&q->wr);

	return b;
}

/*
 *  throw away the next 'len' bytes in the queue
 */
int
qdiscard(Queue *q, int len)
{
	Block *b, *tofree = nil;
	int dowakeup, n, sofar;

	ilock(q);
	for(sofar = 0; sofar < len; sofar += n){
		b = q->bfirst;
		if(b == nil)
			break;
		QDEBUG checkb(b, "qdiscard");
		n = BLEN(b);
		if(n <= len - sofar){
			q->bfirst = b->next;
			q->len -= BALLOC(b);
			q->dlen -= BLEN(b);

			/* remember to free this */
			b->next = tofree;
			tofree = b;
		} else {
			n = len - sofar;
			b->rp += n;
			q->dlen -= n;
		}
	}

	/*
	 *  if writer flow controlled, restart
	 *
	 *  This used to be
	 *	q->len < q->limit/2
	 *  but it slows down tcp too much for certain write sizes.
	 *  I really don't understand it completely.  It may be
	 *  due to the queue draining so fast that the transmission
	 *  stalls waiting for the app to produce more data.  - presotto
	 */
	if((q->state & Qflow) && q->len < q->limit){
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	iunlock(q);

	if(dowakeup)
		wakeup(&q->wr);

	if(tofree != nil)
		freeblist(tofree);

	return sofar;
}

/*
 *  Interrupt level copy out of a queue, return # bytes copied.
 */
int
qconsume(Queue *q, void *vp, int len)
{
	Block *b, *tofree = nil;
	int n, dowakeup;
	unsigned char *p = vp;

	/* sync with qwrite */
	ilock(q);

	for(;;) {
		b = q->bfirst;
		if(b == nil){
			q->state |= Qstarve;
			len = -1;
			goto out;
		}
		QDEBUG checkb(b, "qconsume 1");

		n = BLEN(b);
		if(n > 0)
			break;
		q->bfirst = b->next;
		q->len -= BALLOC(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	};

	consumecnt += n;
	if(n < len)
		len = n;
	jehanne_memmove(p, b->rp, len);
	b->rp += len;
	q->dlen -= len;

	/* discard the block if we're done with it */
	if((q->state & Qmsg) || len == n){
		q->bfirst = b->next;
		q->len -= BALLOC(b);
		q->dlen -= BLEN(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	}

out:
	/* if writer flow controlled, restart */
	if((q->state & Qflow) && q->len < q->limit/2){
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	iunlock(q);

	if(dowakeup)
		wakeup(&q->wr);

	if(tofree != nil)
		freeblist(tofree);

	return len;
}

int
qpass(Queue *q, Block *b)
{
	int len, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	ilock(q);
	if(q->len >= q->limit){
		iunlock(q);
		freeblist(b);
		return -1;
	}
	if(q->state & Qclosed){
		iunlock(q);
		freeblist(b);
		return 0;
	}

	len = qaddlist(q, b);

	if(q->len >= q->limit/2)
		q->state |= Qflow;

	if(q->state & Qstarve){
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	iunlock(q);

	if(dowakeup)
		wakeup(&q->rr);

	return len;
}

int
qpassnolim(Queue *q, Block *b)
{
	int len, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	ilock(q);

	if(q->state & Qclosed){
		iunlock(q);
		freeblist(b);
		return 0;
	}

	len = qaddlist(q, b);

	if(q->len >= q->limit/2)
		q->state |= Qflow;

	if(q->state & Qstarve){
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	iunlock(q);

	if(dowakeup)
		wakeup(&q->rr);

	return len;
}

/*
 *  if the allocated space is way out of line with the used
 *  space, reallocate to a smaller block
 */
Block*
packblock(Block *bp)
{
	Block **l, *nbp;
	int n;

	for(l = &bp; (nbp = *l) != nil; l = &(*l)->next){
		n = BLEN(nbp);
		if((n<<2) < BALLOC(nbp)){
			*l = allocb(n);
			jehanne_memmove((*l)->wp, nbp->rp, n);
			(*l)->wp += n;
			(*l)->next = nbp->next;
			freeb(nbp);
		}
	}

	return bp;
}

int
qproduce(Queue *q, void *vp, int len)
{
	Block *b;
	int dowakeup;
	unsigned char *p = vp;

	b = iallocb(len);
	if(b == nil)
		return 0;

	/* sync with qread */
	dowakeup = 0;
	ilock(q);

	/* no waiting receivers, room in buffer? */
	if(q->len >= q->limit){
		q->state |= Qflow;
		iunlock(q);
		return -1;
	}
	producecnt += len;

	/* save in buffer */
	jehanne_memmove(b->wp, p, len);
	b->wp += len;
	qaddlist(q, b);

	if(q->state & Qstarve){
		q->state &= ~Qstarve;
		dowakeup = 1;
	}

	if(q->len >= q->limit)
		q->state |= Qflow;
	iunlock(q);

	if(dowakeup)
		wakeup(&q->rr);

	return len;
}

/*
 *  copy from offset in the queue
 */
Block*
qcopy(Queue *q, int len, unsigned int offset)
{
	Block *b;

	b = allocb(len);
	ilock(q);
	b->wp += readblist(q->bfirst, b->wp, len, offset);
	iunlock(q);
	return b;
}

/*
 *  called by non-interrupt code
 */
Queue*
qopen(int limit, int msg, void (*kick)(void*), void *arg)
{
	Queue *q;

	q = jehanne_malloc(sizeof(Queue));
	if(q == nil)
		return nil;

	q->limit = q->inilim = limit;
	q->kick = kick;
	q->arg = arg;
	q->state = msg;

	q->state |= Qstarve;
	q->eof = 0;
	q->noblock = 0;

	return q;
}

/* open a queue to be bypassed */
Queue*
qbypass(void (*bypass)(void*, Block*), void *arg)
{
	Queue *q;

	q = jehanne_malloc(sizeof(Queue));
	if(q == nil)
		return nil;

	q->limit = 0;
	q->arg = arg;
	q->bypass = bypass;
	q->state = 0;

	return q;
}

static int
notempty(void *a)
{
	Queue *q = a;

	return (q->state & Qclosed) || q->bfirst != nil;
}

/*
 *  wait for the queue to be non-empty or closed.
 *  called with q ilocked.
 */
static int
qwait(Queue *q)
{
	/* wait for data */
	for(;;){
		if(q->bfirst != nil)
			break;

		if(q->state & Qclosed){
			if(++q->eof > 3)
				return -1;
			if(*q->err && jehanne_strcmp(q->err, Ehungup) != 0)
				return -1;
			return 0;
		}

		q->state |= Qstarve;	/* flag requesting producer to wake me */
		iunlock(q);
		sleep(&q->rr, notempty, q);
		ilock(q);
	}
	return 1;
}

/*
 * add a block list to a queue, return bytes added
 */
int
qaddlist(Queue *q, Block *b)
{
	int len, dlen;

	QDEBUG checkb(b, "qaddlist 1");

	/* queue the block */
	if(q->bfirst != nil)
		q->blast->next = b;
	else
		q->bfirst = b;

	len = BALLOC(b);
	dlen = BLEN(b);
	while(b->next != nil){
		b = b->next;
		QDEBUG checkb(b, "qaddlist 2");

		len += BALLOC(b);
		dlen += BLEN(b);
	}
	q->blast = b;
	q->len += len;
	q->dlen += dlen;
	return dlen;
}

/*
 *  called with q ilocked
 */
Block*
qremove(Queue *q)
{
	Block *b;

	b = q->bfirst;
	if(b == nil)
		return nil;
	QDEBUG checkb(b, "qremove");
	q->bfirst = b->next;
	b->next = nil;
	q->dlen -= BLEN(b);
	q->len -= BALLOC(b);
	return b;
}

/*
 *  copy the contents of a string of blocks into
 *  memory from an offset. blocklist kept unchanged.
 *  return number of copied bytes.
 */
long
readblist(Block *b, unsigned char *p, long n, unsigned int o)
{
	unsigned int m, r;

	r = 0;
	while(n > 0 && b != nil){
		m = BLEN(b);
		if(o >= m)
			o -= m;
		else {
			m -= o;
			if(n < m)
				m = n;
			jehanne_memmove(p, b->rp + o, m);
			p += m;
			r += m;
			n -= m;
			o = 0;
		}
		b = b->next;
	}
	return r;
}

/*
 *  put a block back to the front of the queue
 *  called with q ilocked
 */
void
qputback(Queue *q, Block *b)
{
	b->next = q->bfirst;
	if(q->bfirst == nil)
		q->blast = b;
	q->bfirst = b;
	q->len += BALLOC(b);
	q->dlen += BLEN(b);
}

/*
 *  cut off n bytes from the end of *h. return a new
 *  block with the tail and change *h to refer to the
 *  head.
 */
static Block*
splitblock(Block **h, int n)
{
	Block *a, *b;
	int m;

	a = *h;
	m = BLEN(a) - n;
	if(m < n){
		b = allocb(m);
		jehanne_memmove(b->wp, a->rp, m);
		b->wp += m;
		a->rp += m;
		*h = b;
		return a;
	} else {
		b = allocb(n);
		a->wp -= n;
		jehanne_memmove(b->wp, a->wp, n);
		b->wp += n;
		return b;
	}
}

/*
 *  flow control, get producer going again
 *  called with q ilocked
 */
static void
qwakeup_iunlock(Queue *q)
{
	int dowakeup = 0;

	/* if writer flow controlled, restart */
	if((q->state & Qflow) && q->len < q->limit/2){
		q->state &= ~Qflow;
		dowakeup = 1;
	}

	iunlock(q);

	/* wakeup flow controlled writers */
	if(dowakeup){
		if(q->kick != nil)
			q->kick(q->arg);
		wakeup(&q->wr);
	}
}

/*
 *  get next block from a queue (up to a limit)
 */
Block*
qbread(Queue *q, int len)
{
	Block *b;
	int n;

	eqlock(&q->rlock);
	if(waserror()){
		qunlock(&q->rlock);
		nexterror();
	}

	ilock(q);
	switch(qwait(q)){
	case 0:
		/* queue closed */
		iunlock(q);
		qunlock(&q->rlock);
		poperror();
		return nil;
	case -1:
		/* multiple reads on a closed queue */
		iunlock(q);
		error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	b = qremove(q);
	n = BLEN(b);

	/* split block if it's too big and this is not a message queue */
	if(n > len){
		n -= len;
		if((q->state & Qmsg) == 0)
			qputback(q, splitblock(&b, n));
		else
			b->wp -= n;
	}

	/* restart producer */
	qwakeup_iunlock(q);

	qunlock(&q->rlock);
	poperror();

	return b;
}

/*
 *  read a queue.  if no data is queued, post a Block
 *  and wait on its Rendez.
 */
long
qread(Queue *q, void *vp, int len)
{
	Block *b, *first, **last;
	int m, n;

	eqlock(&q->rlock);
	if(waserror()){
		qunlock(&q->rlock);
		nexterror();
	}

	ilock(q);
again:
	switch(qwait(q)){
	case 0:
		/* queue closed */
		iunlock(q);
		qunlock(&q->rlock);
		poperror();
		return 0;
	case -1:
		/* multiple reads on a closed queue */
		iunlock(q);
		error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	last = &first;
	if(q->state & Qcoalesce){
		/* when coalescing, 0 length blocks just go away */
		b = q->bfirst;
		m = BLEN(b);
		if(m <= 0){
			freeb(qremove(q));
			goto again;
		}

		/*  grab the first block plus as many
		 *  following blocks as will partially
		 *  fit in the read.
		 */
		n = 0;
		for(;;) {
			*last = qremove(q);
			n += m;
			if(n >= len || q->bfirst == nil)
				break;
			last = &b->next;
			b = q->bfirst;
			m = BLEN(b);
		}
	} else {
		first = qremove(q);
		n = BLEN(first);
	}

	/* split last block if it's too big and this is not a message queue */
	if(n > len && (q->state & Qmsg) == 0)
		qputback(q, splitblock(last, n - len));

	/* restart producer */
	qwakeup_iunlock(q);

	qunlock(&q->rlock);
	poperror();

	if(waserror()){
		freeblist(first);
		nexterror();
	}
	n = readblist(first, vp, len, 0);
	freeblist(first);
	poperror();

	return n;
}

static int
qnotfull(void *a)
{
	Queue *q = a;

	return q->len < q->limit || (q->state & Qclosed);
}

/*
 *  flow control, wait for queue to get below the limit
 */
static void
qflow(Queue *q)
{
	for(;;){
		if(q->noblock || qnotfull(q))
			break;

		ilock(q);
		q->state |= Qflow;
		iunlock(q);

		eqlock(&q->wlock);
		if(waserror()){
			qunlock(&q->wlock);
			nexterror();
		}
		sleep(&q->wr, qnotfull, q);
		qunlock(&q->wlock);
		poperror();
	}
}

/*
 *  add a block to a queue obeying flow control
 */
long
qbwrite(Queue *q, Block *b)
{
	int len, dowakeup;
	Proc *p;

	if(q->bypass != nil){
		len = blocklen(b);
		(*q->bypass)(q->arg, b);
		return len;
	}

	dowakeup = 0;
	if(waserror()){
		freeblist(b);
		nexterror();
	}
	ilock(q);

	/* give up if the queue is closed */
	if(q->state & Qclosed){
		iunlock(q);
		error(q->err);
	}

	/* don't queue over the limit */
	if(q->len >= q->limit && q->noblock){
		iunlock(q);
		poperror();
		len = blocklen(b);
		freeblist(b);
		return len;
	}

	len = qaddlist(q, b);

	/* make sure other end gets awakened */
	if(q->state & Qstarve){
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	iunlock(q);
	poperror();

	/*  get output going again */
	if(q->kick != nil && (dowakeup || (q->state&Qkick)))
		q->kick(q->arg);

	/* wakeup anyone consuming at the other end */
	if(dowakeup){
		p = wakeup(&q->rr);

		/* if we just wokeup a higher priority process, let it run */
		if(p != nil && p->priority > up->priority)
			sched();
	}

	/*
	 *  flow control, before allowing the process to continue and
	 *  queue more. We do this here so that postnote can only
	 *  interrupt us after the data has been queued.  This means that
	 *  things like 9p flushes and ssl messages will not be disrupted
	 *  by software interrupts.
	 */
	qflow(q);

	return len;
}

/*
 *  write to a queue.  only Maxatomic bytes at a time is atomic.
 */
int
qwrite(Queue *q, void *vp, int len)
{
	int n, sofar;
	Block *b;
	unsigned char *p = vp;

	QDEBUG if(!islo())
		jehanne_print("qwrite hi %#p\n", getcallerpc());

	/* stop queue bloat before allocating blocks */
	if(q->len/2 >= q->limit && q->noblock == 0 && q->bypass == nil){
		while(waserror()){
			if(up->procctl == Proc_exitme || up->procctl == Proc_exitbig)
				error(Egreg);
		}
		qflow(q);
		poperror();
	}

	sofar = 0;
	do {
		n = len-sofar;
		if(n > Maxatomic)
			n = Maxatomic;

		b = allocb(n);
		if(waserror()){
			freeb(b);
			nexterror();
		}
		jehanne_memmove(b->wp, p+sofar, n);
		poperror();
		b->wp += n;

		sofar += qbwrite(q, b);
	} while(sofar < len && (q->state & Qmsg) == 0);

	return len;
}

/*
 *  used by jehanne_print() to write to a queue.  Since we may be splhi or not in
 *  a process, don't qlock.
 */
int
qiwrite(Queue *q, void *vp, int len)
{
	int n, sofar, dowakeup;
	Block *b;
	unsigned char *p = vp;

	dowakeup = 0;

	sofar = 0;
	do {
		n = len-sofar;
		if(n > Maxatomic)
			n = Maxatomic;

		b = iallocb(n);
		if(b == nil)
			break;
		jehanne_memmove(b->wp, p+sofar, n);
		b->wp += n;

		ilock(q);

		/* we use an artificially high limit for kernel prints since anything
		 * over the limit gets dropped
		 */
		if((q->state & Qclosed) != 0 || q->len/2 >= q->limit){
			iunlock(q);
			freeb(b);
			break;
		}

		qaddlist(q, b);

		if(q->state & Qstarve){
			q->state &= ~Qstarve;
			dowakeup = 1;
		}

		iunlock(q);

		if(dowakeup){
			if(q->kick != nil)
				q->kick(q->arg);
			wakeup(&q->rr);
		}

		sofar += n;
	} while(sofar < len && (q->state & Qmsg) == 0);

	return sofar;
}

/*
 *  be extremely careful when calling this,
 *  as there is no reference accounting
 */
void
qfree(Queue *q)
{
	qclose(q);
	jehanne_free(q);
}

/*
 *  Mark a queue as closed.  No further IO is permitted.
 *  All blocks are released.
 */
void
qclose(Queue *q)
{
	Block *bfirst;

	if(q == nil)
		return;

	/* mark it */
	ilock(q);
	q->state |= Qclosed;
	q->state &= ~(Qflow|Qstarve);
	kstrcpy(q->err, Ehungup, ERRMAX);
	bfirst = q->bfirst;
	q->bfirst = nil;
	q->len = 0;
	q->dlen = 0;
	q->noblock = 0;
	iunlock(q);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	wakeup(&q->rr);
	wakeup(&q->wr);
}

/*
 *  Mark a queue as closed.  Wakeup any readers.  Don't remove queued
 *  blocks.
 */
void
qhangup(Queue *q, char *msg)
{
	/* mark it */
	ilock(q);
	q->state |= Qclosed;
	if(msg == nil || *msg == '\0')
		msg = Ehungup;
	kstrcpy(q->err, msg, ERRMAX);
	iunlock(q);

	/* wake up readers/writers */
	wakeup(&q->rr);
	wakeup(&q->wr);
}

/*
 *  return non-zero if the q is hungup
 */
int
qisclosed(Queue *q)
{
	return q->state & Qclosed;
}

/*
 *  mark a queue as no longer hung up
 */
void
qreopen(Queue *q)
{
	ilock(q);
	q->state &= ~Qclosed;
	q->state |= Qstarve;
	q->eof = 0;
	q->limit = q->inilim;
	iunlock(q);
}

/*
 *  return bytes queued
 */
int
qlen(Queue *q)
{
	return q->dlen;
}

/*
 * return space remaining before flow control
 */
int
qwindow(Queue *q)
{
	int l;

	l = q->limit - q->len;
	if(l < 0)
		l = 0;
	return l;
}

/*
 *  return true if we can read without blocking
 */
int
qcanread(Queue *q)
{
	return q->bfirst != nil;
}

/*
 *  change queue limit
 */
void
qsetlimit(Queue *q, int limit)
{
	q->limit = limit;
}

/*
 *  set blocking/nonblocking
 */
void
qnoblock(Queue *q, int onoff)
{
	q->noblock = onoff;
}

/*
 *  flush the output queue
 */
void
qflush(Queue *q)
{
	Block *bfirst;

	/* mark it */
	ilock(q);
	bfirst = q->bfirst;
	q->bfirst = nil;
	q->len = 0;
	q->dlen = 0;
	iunlock(q);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up writers */
	wakeup(&q->wr);
}

int
qfull(Queue *q)
{
	return q->state & Qflow;
}

int
qstate(Queue *q)
{
	return q->state;
}
