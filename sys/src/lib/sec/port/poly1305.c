#include "os.h"
#include <libsec.h>

/*
	poly1305 implementation using 32 bit * 32 bit = 64 bit multiplication and 64 bit addition

	derived from http://github.com/floodberry/poly1305-donna
*/

#define U8TO32(p)	((uint32_t)(p)[0] | (uint32_t)(p)[1]<<8 | (uint32_t)(p)[2]<<16 | (uint32_t)(p)[3]<<24)
#define U32TO8(p, v)	(p)[0]=(v), (p)[1]=(v)>>8, (p)[2]=(v)>>16, (p)[3]=(v)>>24

/* (r,s) = (key[0:15],key[16:31]), the one time key */
DigestState*
poly1305(uint8_t *m, uint32_t len, uint8_t *key, uint32_t klen, uint8_t *digest, DigestState *s)
{
	uint32_t r0,r1,r2,r3,r4, s1,s2,s3,s4, h0,h1,h2,h3,h4, g0,g1,g2,g3,g4;
	uint64_t d0,d1,d2,d3,d4, f;
	uint32_t hibit, mask, c;

	if(s == nil){
		s = jehanne_malloc(sizeof(*s));
		if(s == nil)
			return nil;
		jehanne_memset(s, 0, sizeof(*s));
		s->malloced = 1;
	}

	if(s->seeded == 0){
		assert(klen == 32);

		/* r &= 0xffffffc0ffffffc0ffffffc0fffffff */
		s->state[0] = (U8TO32(&key[ 0])     ) & 0x3ffffff;
		s->state[1] = (U8TO32(&key[ 3]) >> 2) & 0x3ffff03;
		s->state[2] = (U8TO32(&key[ 6]) >> 4) & 0x3ffc0ff;
		s->state[3] = (U8TO32(&key[ 9]) >> 6) & 0x3f03fff;
		s->state[4] = (U8TO32(&key[12]) >> 8) & 0x00fffff;

		/* h = 0 */
		s->state[5] = 0;
		s->state[6] = 0;
		s->state[7] = 0;
		s->state[8] = 0;
		s->state[9] = 0;

		/* save pad for later */
		s->state[10] = U8TO32(&key[16]);
		s->state[11] = U8TO32(&key[20]);
		s->state[12] = U8TO32(&key[24]);
		s->state[13] = U8TO32(&key[28]);

		s->seeded = 1;
	}

	if(s->blen){
		c = 16 - s->blen;
		if(c > len)
			c = len;
		jehanne_memmove(s->buf + s->blen, m, c);
		len -= c, m += c;
		s->blen += c;
		if(s->blen == 16){
			s->blen = 0;
			poly1305(s->buf, 16, key, klen, nil, s);
		} else if(len == 0){
			m = s->buf;
			len = s->blen;
			s->blen = 0;
		}
	}

	r0 = s->state[0];
	r1 = s->state[1];
	r2 = s->state[2];
	r3 = s->state[3];
	r4 = s->state[4];

	h0 = s->state[5];
	h1 = s->state[6];
	h2 = s->state[7];
	h3 = s->state[8];
	h4 = s->state[9];

	s1 = r1 * 5;
	s2 = r2 * 5;
	s3 = r3 * 5;
	s4 = r4 * 5;

	hibit = 1<<24;	/* 1<<128 */

	while(len >= 16){
Block:
		/* h += m[i] */
		h0 += (U8TO32(&m[0])     ) & 0x3ffffff;
		h1 += (U8TO32(&m[3]) >> 2) & 0x3ffffff;
		h2 += (U8TO32(&m[6]) >> 4) & 0x3ffffff;
		h3 += (U8TO32(&m[9]) >> 6) & 0x3ffffff;
		h4 += (U8TO32(&m[12])>> 8) | hibit;

		/* h *= r */
		d0 = ((uint64_t)h0 * r0) + ((uint64_t)h1 * s4) + ((uint64_t)h2 * s3) + ((uint64_t)h3 * s2) + ((uint64_t)h4 * s1);
		d1 = ((uint64_t)h0 * r1) + ((uint64_t)h1 * r0) + ((uint64_t)h2 * s4) + ((uint64_t)h3 * s3) + ((uint64_t)h4 * s2);
		d2 = ((uint64_t)h0 * r2) + ((uint64_t)h1 * r1) + ((uint64_t)h2 * r0) + ((uint64_t)h3 * s4) + ((uint64_t)h4 * s3);
		d3 = ((uint64_t)h0 * r3) + ((uint64_t)h1 * r2) + ((uint64_t)h2 * r1) + ((uint64_t)h3 * r0) + ((uint64_t)h4 * s4);
		d4 = ((uint64_t)h0 * r4) + ((uint64_t)h1 * r3) + ((uint64_t)h2 * r2) + ((uint64_t)h3 * r1) + ((uint64_t)h4 * r0);

		/* (partial) h %= p */
		              c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
		d1 += c;      c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
		d2 += c;      c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
		d3 += c;      c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
		d4 += c;      c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
		h0 += c * 5;  c = (h0 >> 26); h0 = h0 & 0x3ffffff;
		h1 += c;

		len -= 16, m += 16;
	}

	if(len){
		s->blen = len;
		jehanne_memmove(s->buf, m, len);
	}

	if(digest == nil){
		s->state[5] = h0;
		s->state[6] = h1;
		s->state[7] = h2;
		s->state[8] = h3;
		s->state[9] = h4;
		return s;
	}

	if(len){
		m = s->buf;
		m[len++] = 1;
		while(len < 16)
			m[len++] = 0;
		hibit = 0;
		goto Block;
	}

	             c = h1 >> 26; h1 = h1 & 0x3ffffff;
	h2 +=     c; c = h2 >> 26; h2 = h2 & 0x3ffffff;
	h3 +=     c; c = h3 >> 26; h3 = h3 & 0x3ffffff;
	h4 +=     c; c = h4 >> 26; h4 = h4 & 0x3ffffff;
	h0 += c * 5; c = h0 >> 26; h0 = h0 & 0x3ffffff;
	h1 +=     c;

	/* compute h + -p */
	g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
	g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
	g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
	g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
	g4 = h4 + c - (1 << 26);

	/* select h if h < p, or h + -p if h >= p */
	mask = (g4 >> 31) - 1;
	g0 &= mask;
	g1 &= mask;
	g2 &= mask;
	g3 &= mask;
	g4 &= mask;
	mask = ~mask;
	h0 = (h0 & mask) | g0;
	h1 = (h1 & mask) | g1;
	h2 = (h2 & mask) | g2;
	h3 = (h3 & mask) | g3;
	h4 = (h4 & mask) | g4;

	/* h = h % (2^128) */
	h0 = (h0      ) | (h1 << 26);
	h1 = (h1 >>  6) | (h2 << 20);
	h2 = (h2 >> 12) | (h3 << 14);
	h3 = (h3 >> 18) | (h4 <<  8);
	
	/* digest = (h + pad) % (2^128) */
	f = (uint64_t)h0 + s->state[10]            ; h0 = (uint32_t)f;
	f = (uint64_t)h1 + s->state[11] + (f >> 32); h1 = (uint32_t)f;
	f = (uint64_t)h2 + s->state[12] + (f >> 32); h2 = (uint32_t)f;
	f = (uint64_t)h3 + s->state[13] + (f >> 32); h3 = (uint32_t)f;

	U32TO8(&digest[0], h0);
	U32TO8(&digest[4], h1);
	U32TO8(&digest[8], h2);
	U32TO8(&digest[12], h3);

	if(s->malloced){
		jehanne_memset(s, 0, sizeof(*s));
		jehanne_free(s);
		return nil;
	}

	jehanne_memset(s, 0, sizeof(*s));
	return nil;
}
