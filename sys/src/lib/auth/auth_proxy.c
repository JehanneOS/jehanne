#include <u.h>
#include <lib9.h>
#include <9P2000.h>
#include <auth.h>
#include "authlocal.h"

enum {
	ARgiveup = 100,
};

static uint8_t*
gstring(uint8_t *p, uint8_t *ep, char **s)
{
	uint32_t n;

	if(p == nil)
		return nil;
	if(p+BIT16SZ > ep)
		return nil;
	n = GBIT16(p);
	p += BIT16SZ;
	if(p+n > ep)
		return nil;
	*s = malloc(n+1);
	memmove((*s), p, n);
	(*s)[n] = '\0';
	p += n;
	return p;
}

static uint8_t*
gcarray(uint8_t *p, uint8_t *ep, uint8_t **s, int *np)
{
	uint32_t n;

	if(p == nil)
		return nil;
	if(p+BIT16SZ > ep)
		return nil;
	n = GBIT16(p);
	p += BIT16SZ;
	if(p+n > ep)
		return nil;
	*s = malloc(n);
	if(*s == nil)
		return nil;
	memmove((*s), p, n);
	*np = n;
	p += n;
	return p;
}

void
auth_freeAI(AuthInfo *ai)
{
	if(ai == nil)
		return;
	free(ai->cuid);
	free(ai->suid);
	free(ai->cap);
	free(ai->secret);
	free(ai);
}

static uint8_t*
convM2AI(uint8_t *p, int n, AuthInfo **aip)
{
	uint8_t *e = p+n;
	AuthInfo *ai;

	ai = mallocz(sizeof(*ai), 1);
	if(ai == nil)
		return nil;

	p = gstring(p, e, &ai->cuid);
	p = gstring(p, e, &ai->suid);
	p = gstring(p, e, &ai->cap);
	p = gcarray(p, e, &ai->secret, &ai->nsecret);
	if(p == nil)
		auth_freeAI(ai);
	else
		*aip = ai;
	return p;
}

AuthInfo*
auth_getinfo(AuthRpc *rpc)
{
	AuthInfo *a;

	if(auth_rpc(rpc, "authinfo", nil, 0) != ARok)
		return nil;
	if(convM2AI((uint8_t*)rpc->arg, rpc->narg, &a) == nil){
		werrstr("bad auth info from factotum");
		return nil;
	}
	return a;
}

static int
dorpc(AuthRpc *rpc, char *verb, char *val, int len, AuthGetkey *getkey)
{
	int ret;

	for(;;){
		if((ret = auth_rpc(rpc, verb, val, len)) != ARneedkey && ret != ARbadkey)
			return ret;
		if(getkey == nil)
			return ARgiveup;	/* don't know how */
		if((*getkey)(rpc->arg) < 0)
			return ARgiveup;	/* user punted */
	}
}

/*
 *  this just proxies what the factotum tells it to.
 */
AuthInfo*
fauth_proxy(int fd, AuthRpc *rpc, AuthGetkey *getkey, char *params)
{
	char *buf;
	int m, n, ret;
	AuthInfo *a;
	char oerr[ERRMAX];

	if(rpc == nil){
		werrstr("fauth_proxy - no factotum");
		return nil;
	}

	strcpy(oerr, "UNKNOWN AUTH ERROR");
	sys_errstr(oerr, sizeof oerr);

	if(dorpc(rpc, "start", params, strlen(params), getkey) != ARok){
		werrstr("fauth_proxy start: %r");
		return nil;
	}

	buf = malloc(AuthRpcMax);
	if(buf == nil)
		return nil;
	for(;;){
		switch(dorpc(rpc, "read", nil, 0, getkey)){
		case ARdone:
			free(buf);
			a = auth_getinfo(rpc);
			/* no error, restore whatever was there */
			sys_errstr(oerr, sizeof oerr);
			return a;
		case ARok:
			if(jehanne_write(fd, rpc->arg, rpc->narg) != rpc->narg){
				werrstr("auth_proxy write fd: %r");
				goto Error;
			}
			break;
		case ARphase:
			n = 0;
			memset(buf, 0, AuthRpcMax);
			while((ret = dorpc(rpc, "write", buf, n, getkey)) == ARtoosmall){
				m = atoi(rpc->arg);
				if(m <= n || m > AuthRpcMax)
					break;
				m = jehanne_read(fd, buf + n, m - n);
				if(m <= 0){
					if(m == 0)
						werrstr("auth_proxy short read");
					else
						werrstr("auth_proxy read fd: %r");
					goto Error;
				}
				n += m;
			}
			if(ret != ARok){
				werrstr("auth_proxy rpc write: %r");
				goto Error;
			}
			break;
		default:
			werrstr("auth_proxy rpc: %r");
			goto Error;
		}
	}
Error:
	free(buf);
	return nil;
}

AuthInfo*
auth_proxy(int fd, AuthGetkey *getkey, char *fmt, ...)
{
	int afd;
	char *p;
	va_list arg;
	AuthInfo *ai;
	AuthRpc *rpc;

	quotefmtinstall();	/* just in case */
	va_start(arg, fmt);
	p = vsmprint(fmt, arg);
	va_end(arg);

	if(p == nil){
		werrstr("not enough memory: %r");
		return nil;
	}

	ai = nil;
	afd = sys_open("/mnt/factotum/rpc", ORDWR);
	if(afd < 0){
		werrstr("opening /mnt/factotum/rpc: %r");
		free(p);
		return nil;
	}

	rpc = auth_allocrpc(afd);
	if(rpc){
		ai = fauth_proxy(fd, rpc, getkey, p);
		auth_freerpc(rpc);
	}
	sys_close(afd);
	free(p);
	return ai;
}
