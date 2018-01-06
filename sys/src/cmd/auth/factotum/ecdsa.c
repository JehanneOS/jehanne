/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "dat.h"

enum {
	CHaveKey,
	CHaveText,
	Maxphase
};

static ECdomain dom;

static char *phasenames[] = {
	"CHaveKey",
	"CHaveText",
};

struct State {
	ECpriv p;
	uint8_t buf[100];
	int n;
};

static int
decryptkey(Fsstate *fss, char *key, char *password)
{
	uint8_t keyenc[53], hash[32];
	AESstate s;
	State *st;

	if(base58dec(key, keyenc, 53) < 0)
		return failure(fss, "invalid base58");
	sha2_256((uint8_t *)password, strlen(password), hash, nil);
	sha2_256(hash, 32, hash, nil);
	setupAESstate(&s, hash, 32, keyenc+37);
	aesCBCdecrypt(keyenc, 37, &s);
	if(keyenc[0] != 0x80)
		return RpcNeedkey;
	sha2_256(keyenc, 33, hash, nil);
	sha2_256(hash, 32, hash, nil);
	if(memcmp(keyenc + 33, hash, 4) != 0)
		return RpcNeedkey;
	st = fss->ps;
	st->p.d = betomp(keyenc + 1, 32, nil);
	st->p.x = mpnew(0);
	st->p.y = mpnew(0);
	ecmul(&dom, &dom.G, st->p.d, &st->p);
	return RpcOk;
}

static int
ecdsainit(Proto * _, Fsstate *fss)
{
	int iscli;
	Key *k;
	Keyinfo ki;
	int ret;
	char *key, *password;
	Attr *attr;

	if(dom.p == nil)
		ecdominit(&dom, secp256k1);
	fss->ps = nil;
	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);
	if(iscli==0)
		return failure(fss, "ecdsa server unimplemented");
	mkkeyinfo(&ki, fss, nil);
	ret = findkey(&k, &ki, "key? !password?");
	if(ret == RpcOk){
		key = _strfindattr(k->attr, "key");
		password = _strfindattr(k->privattr, "!password");

	}else{
		if(!_strfindattr(fss->attr, "dom"))
			return ret;
		attr = _copyattr(fss->attr);
		_delattr(attr, "key");
		mkkeyinfo(&ki, fss, attr);
		ret = findkey(&k, &ki, "dom? !password?");
		if(ret != RpcOk)
			return ret;
		key = _strfindattr(fss->attr, "key");
		password = _strfindattr(k->privattr, "!password");
	}
	if(key == nil || password == nil)
		return RpcNeedkey;
	fss->ps = emalloc(sizeof(State));
	ret = decryptkey(fss, key, password);
	if(ret != RpcOk)
		return ret;
	
	setattrs(fss->attr, k->attr);
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	fss->phase = CHaveKey;
	return RpcOk;
}

static void
derencode(mpint *r, mpint *s, uint8_t *buf, int *n)
{
	uint8_t rk[33], sk[33];
	int rl, sl;
	
	mptobe(r, rk, 32, nil);
	mptobe(s, sk, 32, nil);
	rl = (mpsignif(r) + 7)/8;
	sl = (mpsignif(s) + 7)/8;
	if(rk[0] & 0x80){
		memmove(rk + 1, rk, 32);
		rk[0] = 0;
		rl++;
	}
	if(sk[0] & 0x80){
		memmove(sk + 1, sk, 32);
		sk[0] = 0;
		sl++;
	}
	buf[0] = 0x30;
	buf[1] = 4 + rl + sl;
	buf[2] = 0x02;
	buf[3] = rl;
	memmove(buf + 4, rk, rl);
	buf[4 + rl] = 0x02;
	buf[5 + rl] = sl;
	memmove(buf + 6 + rl, sk, sl);
	*n = 6 + rl + sl;
}

static int
ecdsawrite(Fsstate *fss, void *va, uint32_t n)
{
	State *st;
	mpint *r, *s;
	
	st = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");
	case CHaveKey:
		r = mpnew(0);
		s = mpnew(0);
		ecdsasign(&dom, &st->p, va, n, r, s);
		derencode(r, s, st->buf, &st->n);
		mpfree(r);
		mpfree(s);
		fss->phase = CHaveText;
		return RpcOk;
	}
}

static int
ecdsaread(Fsstate *fss, void *va, uint32_t *n)
{
	State *st;
	
	st = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");
	case CHaveText:
		if(*n > st->n)
			*n = st->n;
		memcpy(va, st->buf, *n);
		fss->phase = Established;
		return RpcOk;
	}
}

static void
ecdsaclose(Fsstate *fss)
{
	State *st;
	
	st = fss->ps;
	if(st == nil)
		return;
	if(st->p.x != nil){
		mpfree(st->p.x);
		mpfree(st->p.y);
		mpfree(st->p.d);
	}
	free(st);
	fss->ps = nil;
}

Proto ecdsa = {
	.name = "ecdsa",
	.init = ecdsainit,
	.read = ecdsaread,
	.write = ecdsawrite,
	.close = ecdsaclose,
	.addkey = replacekey,
	.keyprompt= "key? !password?",
};
