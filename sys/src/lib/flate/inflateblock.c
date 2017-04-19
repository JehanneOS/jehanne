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
blgetc(void *vb)
{
	Block *b;

	b = vb;
	if(b->pos >= b->limit)
		return -1;
	return *b->pos++;
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
inflateblock(uint8_t *dst, int dsize, uint8_t *src, int ssize)
{
	Block bd, bs;
	int ok;

	bs.pos = src;
	bs.limit = src + ssize;

	bd.pos = dst;
	bd.limit = dst + dsize;

	ok = inflate(&bd, blwrite, &bs, blgetc);
	if(ok != FlateOk)
		return ok;
	return bd.pos - dst;
}
