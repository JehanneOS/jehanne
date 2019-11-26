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
 * CHAP, MSCHAP
 * 
 * The client does not authenticate the server, hence no CAI
 *
 * Client protocol:
 *	write Chapchal 
 *	read response Chapreply or MSchaprely structure
 *
 * Server protocol:
 *	read challenge: 8 bytes binary
 *	write user: utf8
 *	write response: Chapreply or MSchapreply structure
 */

#include <ctype.h>
#include "dat.h"

enum {
	ChapChallen = 8,
	ChapResplen = 16,

	/* Microsoft auth constants */
	MShashlen = 16,
	MSchallen = 8,
	MSresplen = 24,
	MSchallenv2 = 16,

	Chapreplylen = MD5LEN+1,
	MSchapreplylen = 24+24,
};

static int dochal(State *s);
static int doreply(State *s, uint8_t *reply, int nreply);
static int dochap(char *passwd, int id, char chal[ChapChallen], uint8_t *resp, int resplen);
static int domschap(char *passwd, uint8_t chal[MSchallen], uint8_t *resp, int resplen);
static int domschap2(char *passwd, char *user, char *dom, uint8_t chal[MSchallen], uint8_t *resp, int resplen);

struct State
{
	int astype;
	int asfd;
	Key *key;
	Authkey k;
	Ticket t;
	Ticketreq tr;
	char chal[ChapChallen];
	int nresp;
	uint8_t resp[4096];
	char err[ERRMAX];
	char user[64];
	uint8_t secret[16];	/* for mschap */
	int nsecret;
};

enum
{
	CNeedChal,
	CHaveResp,

	SHaveChal,
	SNeedUser,
	SNeedResp,
	SHaveZero,
	SHaveCAI,

	Maxphase
};

static char *phasenames[Maxphase] =
{
[CNeedChal]	"CNeedChal",
[CHaveResp]	"CHaveResp",

[SHaveChal]	"SHaveChal",
[SNeedUser]	"SNeedUser",
[SNeedResp]	"SNeedResp",
[SHaveZero]	"SHaveZero",
[SHaveCAI]	"SHaveCAI",
};

static int
chapinit(Proto *p, Fsstate *fss)
{
	int iscli, ret;
	State *s;

	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);

	if(!iscli && p == &mschapv2)
		return failure(fss, "role must be client");

	s = emalloc(sizeof *s);
	s->nresp = 0;
	s->nsecret = 0;
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	s->asfd = -1;
	if(p == &mschap || p == &mschapv2 || p == &mschap2){
		s->astype = AuthMSchap;
	}else {
		s->astype = AuthChap;
	}
	if(iscli)
		fss->phase = CNeedChal;
	else{
		if((ret = findp9authkey(&s->key, fss)) != RpcOk){
			free(s);
			return ret;
		}
		if(dochal(s) < 0){
			free(s);
			return failure(fss, nil);
		}
		fss->phase = SHaveChal;
	}

	fss->ps = s;
	return RpcOk;
}

static void
chapclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->asfd >= 0){
		sys_close(s->asfd);
		s->asfd = -1;
	}
	free(s);
}

static int
chapwrite(Fsstate *fss, void *va, uint32_t n)
{
	int ret, nreply;
	char *a, *v;
	Key *k;
	Keyinfo ki;
	State *s;
	Chapreply *cr;
	MSchapreply *mcr;
	OChapreply *ocr;
	OMSchapreply *omcr;
	uint8_t reply[4096];
	char *user, *dom;

	s = fss->ps;
	a = va;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");

	case CNeedChal:
		ret = findkey(&k, mkkeyinfo(&ki, fss, nil), "%s", fss->proto->keyprompt);
		if(ret != RpcOk)
			return ret;
		v = _strfindattr(k->privattr, "!password");
		if(v == nil){
			closekey(k);
			return failure(fss, "key has no password");
		}
		s->nresp = 0;
		memset(s->resp, 0, sizeof(s->resp));
		setattrs(fss->attr, k->attr);
		switch(s->astype){
		case AuthMSchap:
			if(n < MSchallen)
				break;
			if(fss->proto == &mschap2){
				user = _strfindattr(fss->attr, "user");
				if(user == nil)
					break;
				dom = _strfindattr(fss->attr, "windom");
				if(dom == nil)
					dom = "";
				s->nresp = domschap2(v, user, dom, (uint8_t*)a, s->resp, sizeof(s->resp));
			}
			else if(fss->proto == &mschapv2 || n == MSchallenv2){
				uint8_t pchal[MSchallenv2];
				DigestState *ds;

				if(n < MSchallenv2)
					break;
				user = _strfindattr(fss->attr, "user");
				if(user == nil)
					break;

				genrandom((uint8_t*)pchal, MSchallenv2);

				/* ChallengeHash() */
				ds = sha1(pchal, MSchallenv2, nil, nil);
				ds = sha1((uint8_t*)a, MSchallenv2, nil, ds);
				sha1((uint8_t*)user, strlen(user), reply, ds);

				s->nresp = domschap(v, reply, s->resp, sizeof(s->resp));
				if(s->nresp <= 0)
					break;

				mcr = (MSchapreply*)s->resp;
				memset(mcr->LMresp, 0, sizeof(mcr->LMresp));
				memmove(mcr->LMresp, pchal, MSchallenv2);
			}
			else {
				s->nresp = domschap(v, (uint8_t*)a, s->resp, sizeof(s->resp));
			}
			break;
		case AuthChap:
			if(n < ChapChallen+1)
				break;
			s->nresp = dochap(v, *a, a+1, s->resp, sizeof(s->resp));
			break;
		}
		closekey(k);
		if(s->nresp <= 0)
			return failure(fss, "chap botch");
		fss->phase = CHaveResp;
		return RpcOk;

	case SNeedUser:
		if(n >= sizeof s->user)
			return failure(fss, "user name too int32_t");
		memmove(s->user, va, n);
		s->user[n] = '\0';
		fss->phase = SNeedResp;
		return RpcOk;

	case SNeedResp:
		switch(s->astype){
		default:
			return failure(fss, "chap internal botch");
		case AuthChap:
			if(n < Chapreplylen)
				return failure(fss, "did not get Chapreply");
			cr = (Chapreply*)va;
			nreply = OCHAPREPLYLEN;
			memset(reply, 0, nreply);
			ocr = (OChapreply*)reply;
			strecpy(ocr->uid, ocr->uid+sizeof(ocr->uid), s->user);
			ocr->id = cr->id;
			memmove(ocr->resp, cr->resp, sizeof(ocr->resp));
			break;
		case AuthMSchap:
			if(n < MSchapreplylen)
				return failure(fss, "did not get MSchapreply");
			if(n > sizeof(reply)+MSchapreplylen-OMSCHAPREPLYLEN)
				return failure(fss, "MSchapreply too int32_t");
			mcr = (MSchapreply*)va;
			nreply = n+OMSCHAPREPLYLEN-MSchapreplylen;
			memset(reply, 0, nreply);
			omcr = (OMSchapreply*)reply;
			strecpy(omcr->uid, omcr->uid+sizeof(omcr->uid), s->user);
			memmove(omcr->LMresp, mcr->LMresp, sizeof(omcr->LMresp));
			memmove(omcr->NTresp, mcr->NTresp, n+sizeof(mcr->NTresp)-MSchapreplylen);
			break;
		}
		if(doreply(s, reply, nreply) < 0)
			return failure(fss, nil);
		fss->phase = Established;
		fss->ai.cuid = s->t.cuid;
		fss->ai.suid = s->t.suid;
		fss->ai.secret = s->secret;
		fss->ai.nsecret = s->nsecret;
		fss->haveai = 1;
		return RpcOk;
	}
}

static int
chapread(Fsstate *fss, void *va, uint32_t *n)
{
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case CHaveResp:
		if(*n > s->nresp)
			*n = s->nresp;
		memmove(va, s->resp, *n);
		fss->phase = Established;
		fss->haveai = 0;
		return RpcOk;

	case SHaveChal:
		if(*n > sizeof s->chal)
			*n = sizeof s->chal;
		memmove(va, s->chal, *n);
		fss->phase = SNeedUser;
		return RpcOk;
	}
}

static int
dochal(State *s)
{
	char *dom, *user;
	int n;

	s->asfd = -1;

	/* send request to authentication server and get challenge */
	if((dom = _strfindattr(s->key->attr, "dom")) == nil
	|| (user = _strfindattr(s->key->attr, "user")) == nil){
		werrstr("chap/dochal cannot happen");
		goto err;
	}
	memmove(&s->k, s->key->priv, sizeof(Authkey));

	memset(&s->tr, 0, sizeof(s->tr));
	safecpy(s->tr.authdom, dom, sizeof(s->tr.authdom));
	safecpy(s->tr.hostid, user, sizeof(s->tr.hostid));
	s->tr.type = s->astype;

	s->asfd = _authreq(&s->tr, &s->k);
	if(s->asfd < 0)
		goto err;
	
	sys_alarm(30*1000);
	n = readn(s->asfd, s->chal, sizeof s->chal);
	sys_alarm(0);
	if(n != sizeof s->chal)
		goto err;

	return 0;

err:
	if(s->asfd >= 0)
		sys_close(s->asfd);
	s->asfd = -1;
	return -1;
}

static int
doreply(State *s, uint8_t *reply, int nreply)
{
	int n;
	Authenticator a;

	sys_alarm(30*1000);
	if(jehanne_write(s->asfd, reply, nreply) != nreply){
		sys_alarm(0);
		goto err;
	}
	n = _asgetresp(s->asfd, &s->t, &a, &s->k);
	if(n < 0){
		sys_alarm(0);
		/* leave connection open so we can try again */
		return -1;
	}
	s->nsecret = readn(s->asfd, s->secret, sizeof s->secret);
	sys_alarm(0);
	if(s->nsecret < 0)
		s->nsecret = 0;
	sys_close(s->asfd);
	s->asfd = -1;

	if(s->t.num != AuthTs
	|| tsmemcmp(s->t.chal, s->tr.chal, sizeof(s->t.chal)) != 0){
		if(s->key->successes == 0)
			disablekey(s->key);
		werrstr(Easproto);
		return -1;
	}
	s->key->successes++;
	if(a.num != AuthAc
	|| tsmemcmp(a.chal, s->tr.chal, sizeof(a.chal)) != 0){
		werrstr(Easproto);
		return -1;
	}
	return 0;
err:
	if(s->asfd >= 0)
		sys_close(s->asfd);
	s->asfd = -1;
	return -1;
}

Proto chap = {
.name=	"chap",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "!password?"
};

Proto mschap = {
.name=	"mschap",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "!password?"
};

Proto mschapv2 = {
.name=	"mschapv2",
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "user? !password?"
};

Proto mschap2 = {
.name=	"mschap2",	/* really NTLMv2 */
.init=	chapinit,
.write=	chapwrite,
.read=	chapread,
.close=	chapclose,
.addkey= replacekey,
.keyprompt= "user? windom? !password?"
};

static void
nthash(uint8_t hash[MShashlen], char *passwd)
{
	DigestState *ds;
	uint8_t b[2];
	Rune r;

	ds = md4(nil, 0, nil, nil);
	while(*passwd){
		passwd += chartorune(&r, passwd);
		b[0] = r & 0xff;
		b[1] = r >> 8;
		md4(b, 2, nil, ds);
	}
	md4(nil, 0, hash, ds);
}

static void
ntv2hash(uint8_t hash[MShashlen], char *passwd, char *user, char *dom)
{
	uint8_t v1hash[MShashlen];
	DigestState *ds;
	uint8_t b[2];
	Rune r;

	nthash(v1hash, passwd);

	/*
	 * Some documentation insists that the username must be forced to
	 * uppercase, but the domain name should not be. Other shows both
	 * being forced to uppercase. I am pretty sure this is irrevevant as the
	 * domain name passed from the remote server always seems to be in
	 * uppercase already.
	 */
        ds = hmac_md5(nil, 0, v1hash, sizeof(v1hash), nil, nil);
	while(*user){
		user += chartorune(&r, user);
		r = toupperrune(r);
		b[0] = r & 0xff;
		b[1] = r >> 8;
        	hmac_md5(b, 2, v1hash, sizeof(v1hash), nil, ds);
	}
	while(*dom){
		dom += chartorune(&r, dom);
		b[0] = r & 0xff;
		b[1] = r >> 8;
        	hmac_md5(b, 2, v1hash, sizeof(v1hash), nil, ds);
	}
        hmac_md5(nil, 0, v1hash, sizeof(v1hash), hash, ds);
}

static void
desencrypt(uint8_t data[8], uint8_t key[7])
{
	uint32_t ekey[32];

	key_setup(key, ekey);
	block_cipher(ekey, data, 0);
}

static void
lmhash(uint8_t hash[MShashlen], char *passwd)
{
	uint8_t buf[14];
	char *stdtext = "KGS!@#$%";
	int i;

	memset(buf, 0, sizeof(buf));
	strncpy((char*)buf, passwd, sizeof(buf));
	for(i=0; i<sizeof(buf); i++)
		if(buf[i] >= 'a' && buf[i] <= 'z')
			buf[i] += 'A' - 'a';

	memcpy(hash, stdtext, 8);
	memcpy(hash+8, stdtext, 8);

	desencrypt(hash, buf);
	desencrypt(hash+8, buf+7);
}

static void
mschalresp(uint8_t resp[MSresplen], uint8_t hash[MShashlen], uint8_t chal[MSchallen])
{
	int i;
	uint8_t buf[21];

	memset(buf, 0, sizeof(buf));
	memcpy(buf, hash, MShashlen);

	for(i=0; i<3; i++) {
		memmove(resp+i*MSchallen, chal, MSchallen);
		desencrypt(resp+i*MSchallen, buf+i*7);
	}
}

static int
domschap(char *passwd, uint8_t chal[MSchallen], uint8_t *resp, int resplen)
{
	uint8_t hash[MShashlen];
	MSchapreply *r;

	r = (MSchapreply*)resp;
	if(resplen < MSchapreplylen)
		return 0;

	lmhash(hash, passwd);
	mschalresp((uint8_t*)r->LMresp, hash, chal);

	nthash(hash, passwd);
	mschalresp((uint8_t*)r->NTresp, hash, chal);

	return MSchapreplylen;
}

static int
domschap2(char *passwd, char *user, char *dom, uint8_t chal[MSchallen], uint8_t *resp, int resplen)
{
	uint8_t hash[MShashlen], *p, *e;
	MSchapreply *r;
	DigestState *s;
	uint64_t t;
	Rune rr;
	int nb;

	ntv2hash(hash, passwd, user, dom);

	r = (MSchapreply*)resp;
	p = (uint8_t*)r->NTresp+16;
	e = resp + resplen;

	if(p+2+2+4+8+8+4+4+4+4 > e)
		return 0;	

	*p++ = 1;		/* 8bit: response type */
	*p++ = 1;		/* 8bit: max response type understood by client */

	*p++ = 0;		/* 16bit: reserved */
	*p++ = 0;

	*p++ = 0;		/* 32bit: unknown */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	t = time(nil);
	t += 11644473600LL;
	t *= 10000000LL;

	*p++ = t;		/* 64bit: time in NT format */
	*p++ = t >> 8;
	*p++ = t >> 16;
	*p++ = t >> 24;
	*p++ = t >> 32;
	*p++ = t >> 40;
	*p++ = t >> 48;
	*p++ = t >> 56;

	genrandom(p, 8);
	p += 8;			/* 64bit: client nonce */

	*p++ = 0;		/* 32bit: unknown data */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	*p++ = 2;		/* AvPair Domain */
	*p++ = 0;
	*p++ = 0;		/* length */
	*p++ = 0;
	nb = 0;
	while(*dom){
		dom += chartorune(&rr, dom);
		if(p+2+4+4 > e)
			return 0;
		*p++ = rr & 0xFF;
		*p++ = rr >> 8;
		nb += 2;
	}
	p[-nb - 2] = nb & 0xFF;
	p[-nb - 1] = nb >> 8;

	*p++ = 0;		/* AvPair EOF */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;
	
	*p++ = 0;		/* 32bit: unknown data */
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	/*
	 * LmResponse = Cat(HMAC_MD5(LmHash, Cat(SC, CC)), CC)
	 */
	s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
	genrandom((uint8_t*)r->LMresp+16, 8);
	hmac_md5((uint8_t*)r->LMresp+16, 8, hash, MShashlen, (uint8_t*)r->LMresp, s);

	/*
	 * NtResponse = Cat(HMAC_MD5(NtHash, Cat(SC, NtBlob)), NtBlob)
	 */
	s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
	hmac_md5((uint8_t*)r->NTresp+16, p - ((uint8_t*)r->NTresp+16), hash, MShashlen, (uint8_t*)r->NTresp, s);

	return p - resp;
}

static int
dochap(char *passwd, int id, char chal[ChapChallen], uint8_t *resp, int resplen)
{
	char buf[1+ChapChallen+MAXNAMELEN+1];
	int n;

	if(resplen < ChapResplen)
		return 0;

	memset(buf, 0, sizeof buf);
	*buf = id;
	n = strlen(passwd);
	if(n > MAXNAMELEN)
		n = MAXNAMELEN-1;
	strncpy(buf+1, passwd, n);
	memmove(buf+1+n, chal, ChapChallen);
	md5((uint8_t*)buf, 1+n+ChapChallen, resp, nil);

	return ChapResplen;
}
