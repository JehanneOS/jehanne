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
 * p9cr, vnc - textual challenge/response authentication
 *
 * Client protocol:	[currently unimplemented]
 *	write challenge
 *	read response
 *
 * Server protocol:
 *	write user
 *	read challenge
 * 	write response
 */

#include "dat.h"

enum
{
	Maxchal=	64,
};

typedef struct State State;
struct State
{
	Key	*key;
	int	astype;
	int	asfd;
	Authkey	k;
	Ticket	t;
	Ticketreq tr;
	char	chal[Maxchal];
	int	challen;
	char	resp[Maxchal];
	int	resplen;
};

enum
{
	CNeedChal,
	CHaveResp,

	SHaveChal,
	SNeedResp,

	Maxphase,
};

static char *phasenames[Maxphase] =
{
[CNeedChal]	"CNeedChal",
[CHaveResp]	"CHaveResp",

[SHaveChal]	"SHaveChal",
[SNeedResp]	"SNeedResp",
};

static void
p9crclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->asfd >= 0){
		sys_close(s->asfd);
		s->asfd = -1;
	}
	free(s);
}

static int getchal(State*, Fsstate*);

static int
p9crinit(Proto *p, Fsstate *fss)
{
	int iscli, ret;
	char *user;
	State *s;
	Attr *attr;
	Keyinfo ki;

	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);
	
	s = emalloc(sizeof(*s));
	s->asfd = -1;
	if(p == &p9cr){
		s->astype = AuthChal;
		s->challen = NETCHLEN;
	}else if(p == &vnc){
		s->astype = AuthVNC;
		s->challen = Maxchal;
	}else
		abort();

	if(iscli){
		fss->phase = CNeedChal;
		if(p == &p9cr)
			attr = setattr(_copyattr(fss->attr), "proto=p9sk1");
		else
			attr = nil;
		ret = findkey(&s->key, mkkeyinfo(&ki, fss, attr),
			"role=client %s", p->keyprompt);
		_freeattr(attr);
		if(ret != RpcOk){
			free(s);
			return ret;
		}
		fss->ps = s;
	}else{
		if((ret = findp9authkey(&s->key, fss)) != RpcOk){
			free(s);
			return ret;
		}
		if((user = _strfindattr(fss->attr, "user")) == nil){
			free(s);
			return failure(fss, "no user name specified in start msg");
		}
		if(strlen(user) >= sizeof s->tr.uid){
			free(s);
			return failure(fss, "user name too int32_t");
		}
		fss->ps = s;
		strcpy(s->tr.uid, user);
		ret = getchal(s, fss);
		if(ret != RpcOk){
			p9crclose(fss);	/* frees s */
			fss->ps = nil;
		}
	}
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	return ret;
}

static int
p9crread(Fsstate *fss, void *va, uint32_t *n)
{
	int m;
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case CHaveResp:
		if(s->resplen < *n)
			*n = s->resplen;
		memmove(va, s->resp, *n);
		fss->phase = Established;
		return RpcOk;

	case SHaveChal:
		if(s->astype == AuthChal)
			m = strlen(s->chal);	/* ascii string */
		else
			m = s->challen;		/* fixed length binary */
		if(m > *n)
			return toosmall(fss, m);
		*n = m;
		memmove(va, s->chal, m);
		fss->phase = SNeedResp;
		return RpcOk;
	}
}

static int
p9response(Fsstate *fss, State *s)
{
	Authkey *akey;
	uint8_t buf[8];
	uint32_t chal;

	memset(buf, 0, 8);
	sprint((char*)buf, "%d", atoi(s->chal));
	akey = (Authkey*)s->key->priv;
	if(encrypt(akey->des, buf, 8) < 0)
		return failure(fss, "can't encrypt response");
	chal = (buf[0]<<24)+(buf[1]<<16)+(buf[2]<<8)+buf[3];
	s->resplen = snprint(s->resp, sizeof s->resp, "%.8lux", chal);
	return RpcOk;
}

static uint8_t tab[256];

/* VNC reverses the bits of each byte before using as a des key */
static void
mktab(void)
{
	int i, j, k;
	static int once;

	if(once)
		return;
	once = 1;

	for(i=0; i<256; i++) {
		j=i;
		tab[i] = 0;
		for(k=0; k<8; k++) {
			tab[i] = (tab[i]<<1) | (j&1);
			j >>= 1;
		}
	}
}

static int
vncaddkey(Key *k, int before)
{
	uint8_t *p;
	char *s;

	k->priv = emalloc(8+1);
	if(s = _strfindattr(k->privattr, "!password")){
		mktab();
		memset(k->priv, 0, 8+1);
		strncpy((char*)k->priv, s, 8);
		for(p=k->priv; *p; p++)
			*p = tab[*p];
	}else{
		werrstr("no key data");
		return -1;
	}
	return replacekey(k, before);
}

#if 0
static void
vncclosekey(Key *k)		// NOT USED
{
	free(k->priv);
}
#endif

static int
vncresponse(Fsstate* _, State *s)
{
	DESstate des;

	memmove(s->resp, s->chal, sizeof s->chal);
	setupDESstate(&des, s->key->priv, nil);
	desECBencrypt((uint8_t*)s->resp, s->challen, &des);
	s->resplen = s->challen;
	return RpcOk;
}

static int
p9crwrite(Fsstate *fss, void *va, uint32_t n)
{
	State *s;
	char *data = va;
	Authenticator a;
	char resp[Maxchal];
	int ret;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");

	case CNeedChal:
		if(n >= sizeof(s->chal))
			return failure(fss, Ebadarg);
		memset(s->chal, 0, sizeof s->chal);
		memmove(s->chal, data, n);
		s->challen = n;

		if(s->astype == AuthChal)
			ret = p9response(fss, s);
		else
			ret = vncresponse(fss, s);
		if(ret != RpcOk)
			return ret;
		fss->phase = CHaveResp;
		return RpcOk;

	case SNeedResp:
		/* send response to auth server and get ticket */
		if(n > sizeof(resp))
			return failure(fss, Ebadarg);
		memset(resp, 0, sizeof resp);
		memmove(resp, data, n);

		sys_alarm(30*1000);
		if(jehanne_write(s->asfd, resp, s->challen) != s->challen){
			sys_alarm(0);
			return failure(fss, Easproto);
		}
		/* get ticket plus authenticator from auth server */
		ret = _asgetresp(s->asfd, &s->t, &a, &s->k);
		sys_alarm(0);

		if(ret < 0)
			return failure(fss, nil);

		/* check ticket */
		if(s->t.num != AuthTs
		|| tsmemcmp(s->t.chal, s->tr.chal, sizeof(s->t.chal)) != 0){
			if (s->key->successes == 0)
				disablekey(s->key);
			return failure(fss, Easproto);
		}
		s->key->successes++;
		if(a.num != AuthAc
		|| tsmemcmp(a.chal, s->tr.chal, sizeof(a.chal)) != 0)
			return failure(fss, Easproto);

		fss->haveai = 1;
		fss->ai.cuid = s->t.cuid;
		fss->ai.suid = s->t.suid;
		fss->ai.nsecret = 0;
		fss->ai.secret = nil;
		fss->phase = Established;
		return RpcOk;
	}
}

static int
getchal(State *s, Fsstate *fss)
{
	int n;

	memmove(&s->k, s->key->priv, sizeof(Authkey));

	safecpy(s->tr.hostid, _strfindattr(s->key->attr, "user"), sizeof(s->tr.hostid));
	safecpy(s->tr.authdom, _strfindattr(s->key->attr, "dom"), sizeof(s->tr.authdom));
	s->tr.type = s->astype;

	s->asfd = _authreq(&s->tr, &s->k);
	if(s->asfd < 0)
		return failure(fss, Easproto);
	sys_alarm(30*1000);
	n = _asrdresp(s->asfd, s->chal, s->challen);
	sys_alarm(0);
	if(n <= 0){
		if(n == 0)
			werrstr("_asrdresp short read");
		return failure(fss, nil);
	}
	s->challen = n;
	fss->phase = SHaveChal;
	return RpcOk;
}

Proto p9cr =
{
.name=		"p9cr",
.init=		p9crinit,
.write=		p9crwrite,
.read=		p9crread,
.close=		p9crclose,
.keyprompt=	"user? !password?",
};

Proto vnc =
{
.name=		"vnc",
.init=		p9crinit,
.write=		p9crwrite,
.read=		p9crread,
.close=		p9crclose,
.keyprompt=	"!password?",
.addkey=	vncaddkey,
};
