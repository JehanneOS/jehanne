#include <u.h>
#include <lib9.h>
#include <flate.h>

typedef struct Block	Block;

struct Block
{
	uint8_t	*pos;
	uint8_t	*limit;
};

static int
blread(void *vb, void *buf, int n)
{
	Block *b;

	b = vb;
	if(n > b->limit - b->pos)
		n = b->limit - b->pos;
	memmove(buf, b->pos, n);
	b->pos += n;
	return n;
}

static int
blwrite(void *vb, void *buf, int n)
{
	Block *b;

	b = vb;

	if(n > b->limit - b->pos)
		n = b->limit - b->pos;
	memmove(b->pos, buf, n);
	b->pos += n;
	return n;
}

int
deflateblock(uint8_t *dst, int dsize, uint8_t *src, int ssize, int level, int debug)
{
	Block bd, bs;
	int ok;

	bs.pos = src;
	bs.limit = src + ssize;

	bd.pos = dst;
	bd.limit = dst + dsize;

	ok = deflate(&bd, blwrite, &bs, blread, level, debug);
	if(ok != FlateOk)
		return ok;
	return bd.pos - dst;
}
