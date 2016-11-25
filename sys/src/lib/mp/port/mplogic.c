#include "os.h"
#include <mp.h>
#include "dat.h"

/*
	mplogic calculates b1|b2 subject to the
	following flag bits (fl)

	bit 0: subtract 1 from b1
	bit 1: invert b1
	bit 2: subtract 1 from b2
	bit 3: invert b2
	bit 4: add 1 to output
	bit 5: invert output
	
	it inverts appropriate bits automatically
	depending on the signs of the inputs
*/

static void
mplogic(mpint *b1, mpint *b2, mpint *sum, int fl)
{
	mpint *t;
	mpdigit *dp1, *dp2, *dpo, d1, d2, d;
	int c1, c2, co;
	int i;

	assert(((b1->flags | b2->flags | sum->flags) & MPtimesafe) == 0);
	if(b1->sign < 0) fl ^= 0x03;
	if(b2->sign < 0) fl ^= 0x0c;
	sum->sign = (int)(((fl|fl>>2)^fl>>4)<<30)>>31|1;
	if(sum->sign < 0) fl ^= 0x30;
	if(b2->top > b1->top){
		t = b1;
		b1 = b2;
		b2 = t;
		fl = fl >> 2 & 0x03 | fl << 2 & 0x0c | fl & 0x30;
	}
	mpbits(sum, b1->top*Dbits);
	dp1 = b1->p;
	dp2 = b2->p;
	dpo = sum->p;
	c1 = fl & 1;
	c2 = fl >> 2 & 1;
	co = fl >> 4 & 1;
	for(i = 0; i < b1->top; i++){
		d1 = dp1[i] - c1;
		if(i < b2->top)
			d2 = dp2[i] - c2;
		else
			d2 = 0;
		if(d1 != (mpdigit)-1) c1 = 0;
		if(d2 != (mpdigit)-1) c2 = 0;
		if((fl & 2) != 0) d1 ^= -1;
		if((fl & 8) != 0) d2 ^= -1;
		d = d1 | d2;
		if((fl & 32) != 0) d ^= -1;
		d += co;
		if(d != 0) co = 0;
		dpo[i] = d;
	}
	sum->top = i;
	mpnorm(sum);
}

void
mpor(mpint *b1, mpint *b2, mpint *sum)
{
	mplogic(b1, b2, sum, 0);
}

void
mpand(mpint *b1, mpint *b2, mpint *sum)
{
	mplogic(b1, b2, sum, 0x2a);
}

void
mpbic(mpint *b1, mpint *b2, mpint *sum)
{
	mplogic(b1, b2, sum, 0x22);
}

void
mpnot(mpint *b, mpint *r)
{
	mpadd(b, mpone, r);
	r->sign ^= -2;
}

void
mpxor(mpint *b1, mpint *b2, mpint *sum)
{
	mpint *t;
	mpdigit *dp1, *dp2, *dpo, d1, d2, d;
	int c1, c2, co;
	int i, fl;

	assert(((b1->flags | b2->flags | sum->flags) & MPtimesafe) == 0);
	if(b2->top > b1->top){
		t = b1;
		b1 = b2;
		b2 = t;
	}
	fl = (b1->sign & 10) ^ (b2->sign & 12);
	sum->sign = (int)(fl << 28) >> 31;
	mpbits(sum, b1->top*Dbits);
	dp1 = b1->p;
	dp2 = b2->p;
	dpo = sum->p;
	c1 = fl >> 1 & 1;
	c2 = fl >> 2 & 1;
	co = fl >> 3 & 1;
	for(i = 0; i < b1->top; i++){
		d1 = dp1[i] - c1;
		if(i < b2->top)
			d2 = dp2[i] - c2;
		else
			d2 = 0;
		if(d1 != (mpdigit)-1) c1 = 0;
		if(d2 != (mpdigit)-1) c2 = 0;
		d = d1 ^ d2;
		d += co;
		if(d != 0) co = 0;
		dpo[i] = d;
	}
	sum->top = i;
	mpnorm(sum);
}

void
mptrunc(mpint *b, int n, mpint *r)
{
	int d, m, i, c;

	assert(((b->flags | r->flags) & MPtimesafe) == 0);
	mpbits(r, n);
	r->top = DIGITS(n);
	d = n / Dbits;
	m = n % Dbits;
	if(b->sign == -1){
		c = 1;
		for(i = 0; i <= r->top; i++){
			if(i < b->top)
				r->p[i] = ~(b->p[i] - c);
			else
				r->p[i] = -1;
			if(r->p[i] != 0)
				c = 0;
		}
		if(m != 0)
			r->p[d] &= (1<<m) - 1;
	}else if(b->sign == 1){
		if(d >= b->top){
			mpassign(b, r);
			return;
		}
		if(b != r)
			for(i = 0; i < d; i++)
				r->p[i] = b->p[i];
		if(m != 0)
			r->p[d] = b->p[d] & (1<<m)-1;
	}
	r->sign = 1;
	mpnorm(r);
}

void
mpxtend(mpint *b, int n, mpint *r)
{
	int d, m, c, i;

	d = (n - 1) / Dbits;
	m = (n - 1) % Dbits;
	if(d >= b->top){
		mpassign(b, r);
		return;
	}
	mptrunc(b, n, r);
	mpbits(r, n);
	if((r->p[d] & 1<<m) == 0){
		mpnorm(r);
		return;
	}
	r->p[d] |= -(1<<m);
	r->sign = -1;
	c = 1;
	for(i = 0; i < r->top; i++){
		r->p[i] = ~(r->p[i] - c);
		if(r->p[i] != 0)
			c = 0;
	}
	mpnorm(r);
}
