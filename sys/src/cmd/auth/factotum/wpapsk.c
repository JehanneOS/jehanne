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

/*
 * WPA-PSK
 *
 * Client protocol:
 *	write challenge: smac[6] + amac[6] + snonce[32] + anonce[32]
 *	read response: ptk[64]
 *
 * Server protocol:
 *	unimplemented
 */
#include "dat.h"

enum {
	PMKlen = 256/8,
	PTKlen = 512/8,

	Eaddrlen = 6,
	Noncelen = 32,
};

enum
{
	CNeedChal,
	CHaveResp,
	Maxphase,
};

static char *phasenames[Maxphase] = {
[CNeedChal]	"CNeedChal",
[CHaveResp]	"CHaveResp",
};

struct State
{
	uint8_t	resp[PTKlen];
};

static int
hextob(char *s, char **sp, uint8_t *b, int n)
{
	int r;

	n <<= 1;
	for(r = 0; r < n && *s; s++){
		*b <<= 4;
		if(*s >= '0' && *s <= '9')
			*b |= (*s - '0');
		else if(*s >= 'a' && *s <= 'f')
			*b |= 10+(*s - 'a');
		else if(*s >= 'A' && *s <= 'F')
			*b |= 10+(*s - 'A');
		else break;
		if((++r & 1) == 0)
			b++;
	}
	if(sp != nil)
		*sp = s;
	return r >> 1;
}

static void
pass2pmk(char *pass, char *ssid, uint8_t pmk[PMKlen])
{
	if(hextob(pass, nil, pmk, PMKlen) == PMKlen)
		return;
	pbkdf2_x((uint8_t*)pass, strlen(pass), (uint8_t*)ssid, strlen(ssid), 4096, pmk, PMKlen, hmac_sha1, SHA1dlen);
}

static void
prfn(uint8_t *k, int klen, char *a, uint8_t *b, int blen, uint8_t *d, int dlen)
{
	uint8_t r[SHA1dlen], i;
	DigestState *ds;
	int n;

	i = 0;
	while(dlen > 0){
		ds = hmac_sha1((uint8_t*)a, strlen(a)+1, k, klen, nil, nil);
		hmac_sha1(b, blen, k, klen, nil, ds);
		hmac_sha1(&i, 1, k, klen, r, ds);
		i++;
		n = dlen;
		if(n > sizeof(r))
			n = sizeof(r);
		memmove(d, r, n); d += n;
		dlen -= n;
	}
}

static void
calcptk(uint8_t pmk[PMKlen], uint8_t smac[Eaddrlen], uint8_t amac[Eaddrlen], 
	uint8_t snonce[Noncelen],  uint8_t anonce[Noncelen], 
	uint8_t ptk[PTKlen])
{
	uint8_t b[2*Eaddrlen + 2*Noncelen];

	if(memcmp(smac, amac, Eaddrlen) > 0){
		memmove(b + Eaddrlen*0, amac, Eaddrlen);
		memmove(b + Eaddrlen*1, smac, Eaddrlen);
	} else {
		memmove(b + Eaddrlen*0, smac, Eaddrlen);
		memmove(b + Eaddrlen*1, amac, Eaddrlen);
	}
	if(memcmp(snonce, anonce, Eaddrlen) > 0){
		memmove(b + Eaddrlen*2 + Noncelen*0, anonce, Noncelen);
		memmove(b + Eaddrlen*2 + Noncelen*1, snonce, Noncelen);
	} else {
		memmove(b + Eaddrlen*2 + Noncelen*0, snonce, Noncelen);
		memmove(b + Eaddrlen*2 + Noncelen*1, anonce, Noncelen);
	}
	prfn(pmk, PMKlen, "Pairwise key expansion", b, sizeof(b), ptk, PTKlen);
}

static int
wpapskinit(Proto *p, Fsstate *fss)
{
	int iscli;
	State *s;

	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);
	if(!iscli)
		return failure(fss, "%s server not supported", p->name);

	s = emalloc(sizeof *s);
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	fss->phase = CNeedChal;
	fss->ps = s;
	return RpcOk;
}

static int
wpapskwrite(Fsstate *fss, void *va, uint32_t n)
{
	uint8_t pmk[PMKlen], *smac, *amac, *snonce, *anonce;
	char *pass, *essid;
	State *s;
	int ret;
	Key *k;
	Keyinfo ki;
	Attr *attr;

	s = fss->ps;

	if(fss->phase != CNeedChal)
		return phaseerror(fss, "write");
	if(n != (2*Eaddrlen + 2*Noncelen))
		return phaseerror(fss, "bad write size");

	attr = _delattr(_copyattr(fss->attr), "role");
	mkkeyinfo(&ki, fss, attr);
	ret = findkey(&k, &ki, "%s", fss->proto->keyprompt);
	_freeattr(attr);
	if(ret != RpcOk)
		return ret;

	pass = _strfindattr(k->privattr, "!password");
	if(pass == nil)
		return failure(fss, "key has no password");
	essid = _strfindattr(k->attr, "essid");
	if(essid == nil)
		return failure(fss, "key has no essid");
	setattrs(fss->attr, k->attr);
	closekey(k);

	pass2pmk(pass, essid, pmk);

	smac = va;
	amac = smac + Eaddrlen;
	snonce = amac + Eaddrlen;
	anonce = snonce + Noncelen;
	calcptk(pmk, smac, amac, snonce, anonce, s->resp);

	fss->phase = CHaveResp;
	return RpcOk;
}

static int
wpapskread(Fsstate *fss, void *va, uint32_t *n)
{
	State *s;

	s = fss->ps;
	if(fss->phase != CHaveResp)
		return phaseerror(fss, "read");
	if(*n > sizeof(s->resp))
		*n = sizeof(s->resp);
	memmove(va, s->resp, *n);
	fss->phase = Established;
	fss->haveai = 0;
	return RpcOk;
}

static void
wpapskclose(Fsstate *fss)
{
	State *s;
	s = fss->ps;
	free(s);
}

Proto wpapsk = {
.name=		"wpapsk",
.init=		wpapskinit,
.write=		wpapskwrite,
.read=		wpapskread,
.close=		wpapskclose,
.addkey=	replacekey,
.keyprompt=	"!password? essid?"
};
