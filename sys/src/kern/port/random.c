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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>

/* machine specific hardware random number generator */
void (*hwrandbuf)(void*, uint32_t) = nil;

static struct
{
	QLock;
	Chachastate cs;
} *rs;

typedef struct Seedbuf Seedbuf;
struct Seedbuf
{
	uint32_t	randomcount;
	uint8_t		buf[64];
	uint8_t		nbuf;
	uint8_t		next;
	uint16_t	bits;

	SHA2_512state	ds;
};

static void
randomsample(Ureg*_, Timer *t)
{
	Seedbuf *s = t->ta;

	if(s->randomcount == 0 || s->nbuf >= sizeof(s->buf))
		return;
	s->bits = (s->bits<<2) ^ s->randomcount;
	s->randomcount = 0;
	if(++s->next < 8/2)
		return;
	s->next = 0;
	s->buf[s->nbuf++] ^= s->bits;
}

static void
randomseed(void*_)
{
	Seedbuf *s;

	s = secalloc(sizeof(Seedbuf));

	if(hwrandbuf != nil)
		(*hwrandbuf)(s->buf, sizeof(s->buf));

	/* Frequency close but not equal to HZ */
	up->tns = (int64_t)(MS2HZ+3)*1000000LL;
	up->tmode = Tperiodic;
	up->tt = nil;
	up->ta = s;
	up->tf = randomsample;
	timeradd(up);
	while(s->nbuf < sizeof(s->buf)){
		if(++s->randomcount <= 100000)
			continue;
		if(anyhigher())
			sched();
	}
	timerdel(up);

	sha2_512(s->buf, sizeof(s->buf), s->buf, &s->ds);
	setupChachastate(&rs->cs, s->buf, 32, s->buf+32, 12, 20);
	qunlock(rs);

	secfree(s);

	pexit("", 1);
}

void
randominit(void)
{
	rs = secalloc(sizeof(*rs));
	qlock(rs);	/* randomseed() unlocks once seeded */
	kproc("randomseed", randomseed, nil);
}

uint32_t
randomread(void *p, uint32_t n)
{
	Chachastate c;

	if(n == 0)
		return 0;

	if(hwrandbuf != nil)
		(*hwrandbuf)(p, n);

	/* copy chacha state, rekey and increment iv */
	qlock(rs);
	memmove(&c, &rs->cs, sizeof(c));
	chacha_encrypt((uint8_t*)&rs->cs.input[4], 32, &c);
	if(++rs->cs.input[13] == 0)
		if(++rs->cs.input[14] == 0)
			++rs->cs.input[15];
	qunlock(rs);

	/* encrypt the buffer, can fault */
	chacha_encrypt((uint8_t*)p, n, &c);

	/* prevent state leakage */
	memset(&c, 0, sizeof(c));

	return n;
}

/* used by fastrand() */
void
genrandom(uint8_t *p, int n)
{
	randomread(p, n);
}

/* used by rand(),nrand() */
long
lrand(void)
{
	/* xoroshiro128+ algorithm */
	static int seeded = 0;
	static uint64_t s[2];
	static Lock lk;
	uint32_t r;

	if(seeded == 0){
		randomread(s, sizeof(s));
		seeded = (s[0] | s[1]) != 0;
	}

	lock(&lk);
	r = (s[0] + s[1]) >> 33;
	s[1] ^= s[0];
	s[0] = (s[0] << 55 | s[0] >> 9) ^ s[1] ^ (s[1] << 14);
	s[1] = (s[1] << 36 | s[1] >> 28);
	unlock(&lk);

	return r;
}
