#include "os.h"
#include <libsec.h>

enum{
	Blockwords=	SalsaBsize/sizeof(uint32_t)
};

/* little-endian data order */
#define GET4(p)	((((((p)[3]<<8) | (p)[2])<<8) | (p)[1])<<8 | (p)[0])
#define PUT4(p, v)	(((p)[0]=v), (v>>=8), ((p)[1]=v), (v>>=8), ((p)[2]=v), (v>>=8), ((p)[3]=v))

#define ROTATE(v,c) (t = v, (uint32_t)(t << (c)) | (t >> (32 - (c))))

#define ENCRYPT(s, x, y, d) {\
	uint32_t v; \
	uint8_t *sp, *dp; \
	sp = (s); \
	v = GET4(sp); \
	v ^= (x)+(y); \
	dp = (d); \
	PUT4(dp, v); \
}

static uint8_t sigma[16] = "expand 32-byte k";
static uint8_t tau[16] = "expand 16-byte k";

static void
load(uint32_t *d, uint8_t *s, int nw)
{
	int i;

	for(i = 0; i < nw; i++, s+=4)
		d[i] = GET4(s);
}

void
setupSalsastate(Salsastate *s, uint8_t *key, uint32_t keylen, uint8_t *iv, uint32_t ivlen, int rounds)
{
	if(keylen != 256/8 && keylen != 128/8)
		jehanne_sysfatal("invalid salsa key length");
	if(ivlen != 64/8 && ivlen != 128/8 && ivlen != 192/8)
		jehanne_sysfatal("invalid salsa iv length");
	if(rounds == 0)
		rounds = 20;
	s->rounds = rounds;
	if(keylen == 256/8) { /* recommended */
		load(&s->input[0],  sigma+4*0, 1);
		load(&s->input[1],  key +16*0, 4);
		load(&s->input[5],  sigma+4*1, 1);
		load(&s->input[10], sigma+4*2, 1);
		load(&s->input[11], key +16*1, 4);
		load(&s->input[15], sigma+4*3, 1);
	}else{
		load(&s->input[0],  tau +4*0, 1);
		load(&s->input[1],  key, 4);
		load(&s->input[5],  tau +4*1, 1);
		load(&s->input[10], tau +4*2, 1);
		load(&s->input[11], key, 4);
		load(&s->input[15], tau +4*3, 1);
	}
	s->key[0] = s->input[1];
	s->key[1] = s->input[2];
	s->key[2] = s->input[3];
	s->key[3] = s->input[4];
	s->key[4] = s->input[11];
	s->key[5] = s->input[12];
	s->key[6] = s->input[13];
	s->key[7] = s->input[14];

	s->ivwords = ivlen/4;
	s->input[8] = 0;
	s->input[9] = 0;
	if(iv == nil){
		s->input[6] = 0;
		s->input[7] = 0;
	}else
		salsa_setiv(s, iv);
}

static void
hsalsablock(uint8_t h[32], Salsastate *s)
{
	uint32_t x[Blockwords], t;
	int i, rounds;

	rounds = s->rounds;
	x[0] = s->input[0];
	x[1] = s->input[1];
	x[2] = s->input[2];
	x[3] = s->input[3];
	x[4] = s->input[4];
	x[5] = s->input[5];
	x[6] = s->input[6];
	x[7] = s->input[7];
	x[8] = s->input[8];
	x[9] = s->input[9];
	x[10] = s->input[10];
	x[11] = s->input[11];
	x[12] = s->input[12];
	x[13] = s->input[13];
	x[14] = s->input[14];
	x[15] = s->input[15];

	for(i = rounds; i > 0; i -= 2) {
	     x[4] ^= ROTATE( x[0]+x[12], 7);
	     x[8] ^= ROTATE( x[4]+ x[0], 9);
	    x[12] ^= ROTATE( x[8]+ x[4],13);
	     x[0] ^= ROTATE(x[12]+ x[8],18);
	     x[9] ^= ROTATE( x[5]+ x[1], 7);
	    x[13] ^= ROTATE( x[9]+ x[5], 9);
	     x[1] ^= ROTATE(x[13]+ x[9],13);
	     x[5] ^= ROTATE( x[1]+x[13],18);
	    x[14] ^= ROTATE(x[10]+ x[6], 7);
	     x[2] ^= ROTATE(x[14]+x[10], 9);
	     x[6] ^= ROTATE( x[2]+x[14],13);
	    x[10] ^= ROTATE( x[6]+ x[2],18);
	     x[3] ^= ROTATE(x[15]+x[11], 7);
	     x[7] ^= ROTATE( x[3]+x[15], 9);
	    x[11] ^= ROTATE( x[7]+ x[3],13);
	    x[15] ^= ROTATE(x[11]+ x[7],18);
	     x[1] ^= ROTATE( x[0]+ x[3], 7);
	     x[2] ^= ROTATE( x[1]+ x[0], 9);
	     x[3] ^= ROTATE( x[2]+ x[1],13);
	     x[0] ^= ROTATE( x[3]+ x[2],18);
	     x[6] ^= ROTATE( x[5]+ x[4], 7);
	     x[7] ^= ROTATE( x[6]+ x[5], 9);
	     x[4] ^= ROTATE( x[7]+ x[6],13);
	     x[5] ^= ROTATE( x[4]+ x[7],18);
	    x[11] ^= ROTATE(x[10]+ x[9], 7);
	     x[8] ^= ROTATE(x[11]+x[10], 9);
	     x[9] ^= ROTATE( x[8]+x[11],13);
	    x[10] ^= ROTATE( x[9]+ x[8],18);
	    x[12] ^= ROTATE(x[15]+x[14], 7);
	    x[13] ^= ROTATE(x[12]+x[15], 9);
	    x[14] ^= ROTATE(x[13]+x[12],13);
	    x[15] ^= ROTATE(x[14]+x[13],18);
	}

	PUT4(h+0*4, x[0]);
	PUT4(h+1*4, x[5]);
	PUT4(h+2*4, x[10]);
	PUT4(h+3*4, x[15]);
	PUT4(h+4*4, x[6]);
	PUT4(h+5*4, x[7]);
	PUT4(h+6*4, x[8]);
	PUT4(h+7*4, x[9]);
}

void
salsa_setiv(Salsastate *s, uint8_t *iv)
{
	if(s->ivwords == 128/32){
		/* hsalsa 128-bit iv */
		load(&s->input[6], iv, 4);
		return;
	}
	if(s->ivwords == 192/32){
		/* xsalsa with 192-bit iv */
		uint32_t counter[2];
		uint8_t h[32];

		counter[0] = s->input[8];
		counter[1] = s->input[9];

		s->input[1] = s->key[0];
		s->input[2] = s->key[1];
		s->input[3] = s->key[2];
		s->input[4] = s->key[3];
		s->input[11] = s->key[4];
		s->input[12] = s->key[5];
		s->input[13] = s->key[6];
		s->input[14] = s->key[7];

		load(&s->input[6], iv, 4);

		hsalsablock(h, s);
		load(&s->input[1],  h+16*0, 4);
		load(&s->input[11], h+16*1, 4);
		jehanne_memset(h, 0, 32);

		s->input[8] = counter[0];
		s->input[9] = counter[1];

		iv += 16;
	}
	/* 64-bit iv */
	load(&s->input[6], iv, 2);
}

void
salsa_setblock(Salsastate *s, uint64_t blockno)
{
	s->input[8] = blockno;
	s->input[9] = blockno>>32;
}

static void
encryptblock(Salsastate *s, uint8_t *src, uint8_t *dst)
{
	uint32_t x[Blockwords], t;
	int i, rounds;

	rounds = s->rounds;
	x[0] = s->input[0];
	x[1] = s->input[1];
	x[2] = s->input[2];
	x[3] = s->input[3];
	x[4] = s->input[4];
	x[5] = s->input[5];
	x[6] = s->input[6];
	x[7] = s->input[7];
	x[8] = s->input[8];
	x[9] = s->input[9];
	x[10] = s->input[10];
	x[11] = s->input[11];
	x[12] = s->input[12];
	x[13] = s->input[13];
	x[14] = s->input[14];
	x[15] = s->input[15];

	for(i = rounds; i > 0; i -= 2) {
	     x[4] ^= ROTATE( x[0]+x[12], 7);
	     x[8] ^= ROTATE( x[4]+ x[0], 9);
	    x[12] ^= ROTATE( x[8]+ x[4],13);
	     x[0] ^= ROTATE(x[12]+ x[8],18);
	     x[9] ^= ROTATE( x[5]+ x[1], 7);
	    x[13] ^= ROTATE( x[9]+ x[5], 9);
	     x[1] ^= ROTATE(x[13]+ x[9],13);
	     x[5] ^= ROTATE( x[1]+x[13],18);
	    x[14] ^= ROTATE(x[10]+ x[6], 7);
	     x[2] ^= ROTATE(x[14]+x[10], 9);
	     x[6] ^= ROTATE( x[2]+x[14],13);
	    x[10] ^= ROTATE( x[6]+ x[2],18);
	     x[3] ^= ROTATE(x[15]+x[11], 7);
	     x[7] ^= ROTATE( x[3]+x[15], 9);
	    x[11] ^= ROTATE( x[7]+ x[3],13);
	    x[15] ^= ROTATE(x[11]+ x[7],18);
	     x[1] ^= ROTATE( x[0]+ x[3], 7);
	     x[2] ^= ROTATE( x[1]+ x[0], 9);
	     x[3] ^= ROTATE( x[2]+ x[1],13);
	     x[0] ^= ROTATE( x[3]+ x[2],18);
	     x[6] ^= ROTATE( x[5]+ x[4], 7);
	     x[7] ^= ROTATE( x[6]+ x[5], 9);
	     x[4] ^= ROTATE( x[7]+ x[6],13);
	     x[5] ^= ROTATE( x[4]+ x[7],18);
	    x[11] ^= ROTATE(x[10]+ x[9], 7);
	     x[8] ^= ROTATE(x[11]+x[10], 9);
	     x[9] ^= ROTATE( x[8]+x[11],13);
	    x[10] ^= ROTATE( x[9]+ x[8],18);
	    x[12] ^= ROTATE(x[15]+x[14], 7);
	    x[13] ^= ROTATE(x[12]+x[15], 9);
	    x[14] ^= ROTATE(x[13]+x[12],13);
	    x[15] ^= ROTATE(x[14]+x[13],18);
	}

#ifdef FULL_UNROLL
	ENCRYPT(src+0*4, x[0], s->input[0], dst+0*4);
	ENCRYPT(src+1*4, x[1], s->input[1], dst+1*4);
	ENCRYPT(src+2*4, x[2], s->input[2], dst+2*4);
	ENCRYPT(src+3*4, x[3], s->input[3], dst+3*4);
	ENCRYPT(src+4*4, x[4], s->input[4], dst+4*4);
	ENCRYPT(src+5*4, x[5], s->input[5], dst+5*4);
	ENCRYPT(src+6*4, x[6], s->input[6], dst+6*4);
	ENCRYPT(src+7*4, x[7], s->input[7], dst+7*4);
	ENCRYPT(src+8*4, x[8], s->input[8], dst+8*4);
	ENCRYPT(src+9*4, x[9], s->input[9], dst+9*4);
	ENCRYPT(src+10*4, x[10], s->input[10], dst+10*4);
	ENCRYPT(src+11*4, x[11], s->input[11], dst+11*4);
	ENCRYPT(src+12*4, x[12], s->input[12], dst+12*4);
	ENCRYPT(src+13*4, x[13], s->input[13], dst+13*4);
	ENCRYPT(src+14*4, x[14], s->input[14], dst+14*4);
	ENCRYPT(src+15*4, x[15], s->input[15], dst+15*4);
#else
	for(i=0; i<nelem(x); i+=4){
		ENCRYPT(src, x[i], s->input[i], dst);
		ENCRYPT(src+4, x[i+1], s->input[i+1], dst+4);
		ENCRYPT(src+8, x[i+2], s->input[i+2], dst+8);
		ENCRYPT(src+12, x[i+3], s->input[i+3], dst+12);
		src += 16;
		dst += 16;
	}
#endif

	if(++s->input[8] == 0)
		s->input[9]++;
}

void
salsa_encrypt2(uint8_t *src, uint8_t *dst, uint32_t bytes, Salsastate *s)
{
	uint8_t tmp[SalsaBsize];

	for(; bytes >= SalsaBsize; bytes -= SalsaBsize){
		encryptblock(s, src, dst);
		src += SalsaBsize;
		dst += SalsaBsize;
	}
	if(bytes > 0){
		jehanne_memmove(tmp, src, bytes);
		encryptblock(s, tmp, tmp);
		jehanne_memmove(dst, tmp, bytes);
	}
}

void
salsa_encrypt(uint8_t *buf, uint32_t bytes, Salsastate *s)
{
	salsa_encrypt2(buf, buf, bytes, s);
}

void
hsalsa(uint8_t h[32], uint8_t *key, uint32_t keylen, uint8_t nonce[16], int rounds)
{
	Salsastate s[1];

	setupSalsastate(s, key, keylen, nonce, 16, rounds);
	hsalsablock(h, s);
	jehanne_memset(s, 0, sizeof(s));
}
