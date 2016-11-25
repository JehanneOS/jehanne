#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <regexp.h>
#include <mp.h>
#include <libsec.h>
#include <authsrv.h>
#include "authcmdlib.h"

Ndb *db;
char raddr[128];
uint8_t zeros[16];

typedef struct Keyslot Keyslot;
struct Keyslot
{
	Authkey;
	char	id[ANAMELEN];
};
Keyslot hkey, akey, ukey;
uint8_t keyseed[SHA2_256dlen];

char ticketform;

/* Microsoft auth constants */
enum {
	MShashlen = 16,
	MSchallen = 8,
	MSresplen = 24,
};

void	pak(Ticketreq*);
void	ticketrequest(Ticketreq*);
void	challengebox(Ticketreq*);
void	changepasswd(Ticketreq*);
void	apop(Ticketreq*, int);
void	chap(Ticketreq*);
void	mschap(Ticketreq*);
void	vnc(Ticketreq*);
int	speaksfor(char*, char*);
void	replyerror(char*, ...);
void	getraddr(char*);
void	initkeyseed(void);
void	mkkey(Keyslot*);
void	mkticket(Ticketreq*, Ticket*);
void	nthash(uint8_t hash[MShashlen], char *passwd);
void	lmhash(uint8_t hash[MShashlen], char *passwd);
void	ntv2hash(uint8_t hash[MShashlen], char *passwd, char *user, char *dom);
void	mschalresp(uint8_t resp[MSresplen], uint8_t hash[MShashlen], uint8_t chal[MSchallen]);
void	desencrypt(uint8_t data[8], uint8_t key[7]);
void	tickauthreply(Ticketreq*, Authkey*);
void	safecpy(char*, char*, int);

void
main(int argc, char *argv[])
{
	char buf[TICKREQLEN];
	Ticketreq tr;
	int n;

	ARGBEGIN{
	case 'N':
		ticketform = 1;
		break;
	}ARGEND

	strcpy(raddr, "unknown");
	if(argc >= 1)
		getraddr(argv[argc-1]);

	alarm(10*60*1000);	/* kill a connection after 10 minutes */

	private();
	initkeyseed();

	db = ndbopen("/lib/ndb/auth");
	if(db == 0)
		syslog(0, AUTHLOG, "no /lib/ndb/auth");

	for(;;){
		n = readn(0, buf, sizeof(buf));
		if(n <= 0 || convM2TR(buf, n, &tr) <= 0)
			exits(0);
		switch(tr.type){
		case AuthTreq:
			ticketrequest(&tr);
			break;
		case AuthChal:
			challengebox(&tr);
			break;
		case AuthPass:
			changepasswd(&tr);
			break;
		case AuthApop:
			apop(&tr, AuthApop);
			break;
		case AuthChap:
			chap(&tr);
			break;
		case AuthMSchap:
			mschap(&tr);
			break;
		case AuthCram:
			apop(&tr, AuthCram);
			break;
		case AuthVNC:
			vnc(&tr);
			break;
		case AuthPAK:
			pak(&tr);
			continue;
		default:
			syslog(0, AUTHLOG, "unknown ticket request type: %d", tr.type);
			exits(0);
		}
		/* invalidate pak keys */
		akey.id[0] = 0;
		hkey.id[0] = 0;
		ukey.id[0] = 0;
	}
	/* not reached */
}

void
pak1(char *u, Keyslot *k)
{
	uint8_t y[PAKYLEN];
	PAKpriv p;

	safecpy(k->id, u, sizeof(k->id));
	if(!findkey(KEYDB, k->id, k) || tsmemcmp(k->aes, zeros, AESKEYLEN) == 0) {
		/* make one up so caller doesn't know it was wrong */
		mkkey(k);
		authpak_hash(k, k->id);
	}
	authpak_new(&p, k, y, 0);
	if(write(1, y, PAKYLEN) != PAKYLEN)
		exits(0);
	if(readn(0, y, PAKYLEN) != PAKYLEN)
		exits(0);
	if(authpak_finish(&p, k, y))
		exits(0);
}

void
pak(Ticketreq *tr)
{
	static uint8_t ok[1] = {AuthOK};

	if(write(1, ok, 1) != 1)
		exits(0);

	/* invalidate pak keys */
	akey.id[0] = 0;
	hkey.id[0] = 0;
	ukey.id[0] = 0;

	if(tr->hostid[0]) {
		if(tr->authid[0])
			pak1(tr->authid, &akey);
		pak1(tr->hostid, &hkey);
	} else if(tr->uid[0]) {
		pak1(tr->uid, &ukey);
	}

	ticketform = 1;
}

int
getkey(char *u, Keyslot *k)
{
	/* empty user id is an error */
	if(*u == 0)
		exits(0);

	if(k == &hkey && strcmp(u, k->id) == 0)
		return 1;
	if(k == &akey && strcmp(u, k->id) == 0)
		return 1;
	if(k == &ukey && strcmp(u, k->id) == 0)
		return 1;

	if(ticketform != 0)
		exits(0);

	return findkey(KEYDB, u, k);
}

void
ticketrequest(Ticketreq *tr)
{
	char tbuf[2*MAXTICKETLEN+1];
	Ticket t;
	int n;

	if(tr->uid[0] == 0)
		exits(0);
	if(!getkey(tr->authid, &akey)){
		/* make one up so caller doesn't know it was wrong */
		mkkey(&akey);
		syslog(0, AUTHLOG, "tr-fail authid %s", tr->authid);
	}
	if(!getkey(tr->hostid, &hkey)){
		/* make one up so caller doesn't know it was wrong */
		mkkey(&hkey);
		syslog(0, AUTHLOG, "tr-fail hostid %s(%s)", tr->hostid, raddr);
	}
	mkticket(tr, &t);
	if(!speaksfor(tr->hostid, tr->uid)){
		mkkey(&akey);
		mkkey(&hkey);
		syslog(0, AUTHLOG, "tr-fail %s@%s(%s) -> %s@%s no speaks for",
			tr->uid, tr->hostid, raddr, tr->uid, tr->authid);
	}
	n = 0;
	tbuf[n++] = AuthOK;
	t.num = AuthTc;
	n += convT2M(&t, tbuf+n, sizeof(tbuf)-n, &hkey);
	t.num = AuthTs;
	n += convT2M(&t, tbuf+n, sizeof(tbuf)-n, &akey);
	if(write(1, tbuf, n) != n)
		exits(0);

	syslog(0, AUTHLOG, "tr-ok %s@%s(%s) -> %s@%s", tr->uid, tr->hostid, raddr, tr->uid, tr->authid);
}

void
challengebox(Ticketreq *tr)
{
	char kbuf[DESKEYLEN], nkbuf[DESKEYLEN], buf[NETCHLEN+1];
	char *key, *netkey, *err;
	int32_t chal;

	if(tr->uid[0] == 0)
		exits(0);
	key = finddeskey(KEYDB, tr->uid, kbuf);
	netkey = finddeskey(NETKEYDB, tr->uid, nkbuf);
	if(key == nil && netkey == nil){
		/* make one up so caller doesn't know it was wrong */
		genrandom((uint8_t*)nkbuf, DESKEYLEN);
		netkey = nkbuf;
		syslog(0, AUTHLOG, "cr-fail uid %s@%s", tr->uid, raddr);
	}

	if(!getkey(tr->hostid, &hkey)){
		/* make one up so caller doesn't know it was wrong */
		mkkey(&hkey);
		syslog(0, AUTHLOG, "cr-fail hostid %s %s@%s", tr->hostid, tr->uid, raddr);
	}

	/*
	 * challenge-response
	 */
	memset(buf, 0, sizeof(buf));
	buf[0] = AuthOK;
	chal = nfastrand(MAXNETCHAL);
	sprint(buf+1, "%lud", chal);
	if(write(1, buf, NETCHLEN+1) != NETCHLEN+1)
		exits(0);
	if(readn(0, buf, NETCHLEN) != NETCHLEN)
		exits(0);
	if(!(key != nil && netcheck(key, chal, buf))
	&& !(netkey != nil && netcheck(netkey, chal, buf))
	&& (err = secureidcheck(tr->uid, buf)) != nil){
		replyerror("cr-fail %s %s %s", err, tr->uid, raddr);
		logfail(tr->uid);
		return;
	}
	succeed(tr->uid);

	/*
	 *  reply with ticket & authenticator
	 */
	tickauthreply(tr, &hkey);

	syslog(0, AUTHLOG, "cr-ok %s@%s(%s)", tr->uid, tr->hostid, raddr);
}

void
changepasswd(Ticketreq *tr)
{
	char tbuf[MAXTICKETLEN+1], prbuf[MAXPASSREQLEN], *err;
	Passwordreq pr;
	Authkey nkey;
	Ticket t;
	int n, m;

	if(!getkey(tr->uid, &ukey)){
		/* make one up so caller doesn't know it was wrong */
		mkkey(&ukey);
		syslog(0, AUTHLOG, "cp-fail uid %s@%s", tr->uid, raddr);
	}

	/* send back a ticket with a new key */
	mkticket(tr, &t);
	t.num = AuthTp;
	n = 0;
	tbuf[n++] = AuthOK;
	n += convT2M(&t, tbuf+n, sizeof(tbuf)-n, &ukey);
	if(write(1, tbuf, n) != n)
		exits(0);

	/* loop trying passwords out */
	for(;;){
		for(n=0; (m = convM2PR(prbuf, n, &pr, &t)) <= 0; n += m){
			m = -m;
			if(m <= n || m > sizeof(prbuf))
				exits(0);
			m -= n;
			if(readn(0, prbuf+n, m) != m)
				exits(0);
		}
		if(pr.num != AuthPass){
			replyerror("protocol botch1: %s", raddr);
			exits(0);
		}
		passtokey(&nkey, pr.old);
		if(tsmemcmp(ukey.des, nkey.des, DESKEYLEN) != 0){
			replyerror("protocol botch2: %s", raddr);
			continue;
		}
		if(tsmemcmp(ukey.aes, zeros, AESKEYLEN) != 0 && tsmemcmp(ukey.aes, nkey.aes, AESKEYLEN) != 0){
			replyerror("protocol botch3: %s", raddr);
			continue;
		}
		if(*pr.new){
			err = okpasswd(pr.new);
			if(err){
				replyerror("%s %s", err, raddr);
				continue;
			}
			passtokey(&nkey, pr.new);
		}
		if(pr.changesecret && setsecret(KEYDB, tr->uid, pr.secret) == 0){
			replyerror("can't write secret %s", raddr);
			continue;
		}
		if(*pr.new && setkey(KEYDB, tr->uid, &nkey) == 0){
			replyerror("can't write key %s", raddr);
			continue;
		}
		memmove(ukey.des, nkey.des, DESKEYLEN);
		memmove(ukey.aes, nkey.aes, AESKEYLEN);
		break;
	}
	succeed(tr->uid);

	prbuf[0] = AuthOK;
	if(write(1, prbuf, 1) != 1)
		exits(0);
}

static char*
domainname(void)
{
	static char sysname[Maxpath];
	static char *domain;
	int n;

	if(domain != nil)
		return domain;
	if(*sysname)
		return sysname;

	domain = csgetvalue(0, "sys", sysname, "dom", nil);
	if(domain != nil)
		return domain;

	n = readfile("/dev/sysname", sysname, sizeof(sysname)-1);
	if(n < 0){
		strcpy(sysname, "kremvax");
		return sysname;
	}
	sysname[n] = 0;

	return sysname;
}

static int
h2b(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0;
}

void
apop(Ticketreq *tr, int type)
{
	int challen, i, n, tries;
	char *secret, *p;
	Ticketreq treq;
	DigestState *s;
	char sbuf[SECRETLEN];
	char trbuf[TICKREQLEN];
	char buf[MD5dlen*2];
	uint8_t digest[MD5dlen], resp[MD5dlen];
	uint32_t rb[4];
	char chal[256];

	USED(tr);

	/*
	 *  Create a challenge and send it.
	 */
	genrandom((uint8_t*)rb, sizeof(rb));
	p = chal;
	p += snprint(p, sizeof(chal), "<%lux%lux.%lux%lux@%s>",
		rb[0], rb[1], rb[2], rb[3], domainname());
	challen = p - chal;
	print("%c%-5d%s", AuthOKvar, challen, chal);

	/* give user a few attempts */
	for(tries = 0; ; tries++) {
		/*
		 *  get ticket request
		 */
		n = readn(0, trbuf, sizeof(trbuf));
		if(n <= 0 || convM2TR(trbuf, n, &treq) <= 0)
			exits(0);
		tr = &treq;
		if(tr->type != type || tr->uid[0] == 0)
			exits(0);

		/*
		 * read response
		 */
		if(readn(0, buf, MD5dlen*2) != MD5dlen*2)
			exits(0);
		for(i = 0; i < MD5dlen; i++)
			resp[i] = (h2b(buf[2*i])<<4)|h2b(buf[2*i+1]);

		/*
		 * lookup
		 */
		secret = findsecret(KEYDB, tr->uid, sbuf);
		if(!getkey(tr->hostid, &hkey) || secret == nil){
			replyerror("apop-fail bad response %s", raddr);
			logfail(tr->uid);
			if(tries > 5)
				exits(0);
			continue;
		}

		/*
		 *  check for match
		 */
		if(type == AuthCram){
			hmac_md5((uint8_t*)chal, challen,
				(uint8_t*)secret, strlen(secret),
				digest, nil);
		} else {
			s = md5((uint8_t*)chal, challen, 0, 0);
			md5((uint8_t*)secret, strlen(secret), digest, s);
		}
		if(tsmemcmp(digest, resp, MD5dlen) != 0){
			replyerror("apop-fail bad response %s", raddr);
			logfail(tr->uid);
			if(tries > 5)
				exits(0);
			continue;
		}
		break;
	}

	succeed(tr->uid);

	/*
	 *  reply with ticket & authenticator
	 */
	tickauthreply(tr, &hkey);

	if(type == AuthCram)
		syslog(0, AUTHLOG, "cram-ok %s %s", tr->uid, raddr);
	else
		syslog(0, AUTHLOG, "apop-ok %s %s", tr->uid, raddr);
}

enum {
	VNCchallen=	16,
};

/* VNC reverses the bits of each byte before using as a des key */
uint8_t swizzletab[256] = {
 0x0, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
 0x8, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
 0x4, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
 0xc, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
 0x2, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
 0xa, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
 0x6, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
 0xe, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
 0x1, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
 0x9, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
 0x5, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
 0xd, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
 0x3, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
 0xb, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
 0x7, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
 0xf, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

void
vnc(Ticketreq *tr)
{
	uint8_t chal[VNCchallen+6];
	uint8_t reply[VNCchallen];
	char sbuf[SECRETLEN];
	char *secret;
	DESstate s;
	int i;

	if(tr->uid[0] == 0)
		exits(0);

	/*
	 *  Create a challenge and send it.
	 */
	genrandom(chal+6, VNCchallen);
	chal[0] = AuthOKvar;
	sprint((char*)chal+1, "%-5d", VNCchallen);
	if(write(1, chal, sizeof(chal)) != sizeof(chal))
		exits(0);

	/*
	 *  lookup keys (and swizzle bits)
	 */
	memset(sbuf, 0, sizeof(sbuf));
	secret = findsecret(KEYDB, tr->uid, sbuf);
	if(!getkey(tr->hostid, &hkey) || secret == nil){
		mkkey(&hkey);
		genrandom((uint8_t*)sbuf, sizeof(sbuf));
		secret = sbuf;
	}
	for(i = 0; i < 8; i++)
		secret[i] = swizzletab[(uint8_t)secret[i]];

	/*
	 *  get response
	 */
	if(readn(0, reply, sizeof(reply)) != sizeof(reply))
		exits(0);

	/*
	 *  decrypt response and compare
	 */
	setupDESstate(&s, (uint8_t*)secret, nil);
	desECBdecrypt(reply, sizeof(reply), &s);
	if(tsmemcmp(reply, chal+6, VNCchallen) != 0){
		replyerror("vnc-fail bad response %s", raddr);
		logfail(tr->uid);
		return;
	}
	succeed(tr->uid);

	/*
	 *  reply with ticket & authenticator
	 */
	tickauthreply(tr, &hkey);

	syslog(0, AUTHLOG, "vnc-ok %s %s", tr->uid, raddr);
}

void
chap(Ticketreq *tr)
{
	char *secret;
	DigestState *s;
	char sbuf[SECRETLEN];
	uint8_t digest[MD5dlen];
	char chal[CHALLEN];
	OChapreply reply;

	/*
	 *  Create a challenge and send it.
	 */
	genrandom((uint8_t*)chal, sizeof(chal));
	if(write(1, chal, sizeof(chal)) != sizeof(chal))
		exits(0);

	/*
	 *  get chap reply
	 */
	if(readn(0, &reply, sizeof(reply)) < 0)
		exits(0);
	safecpy(tr->uid, reply.uid, sizeof(tr->uid));
	if(tr->uid[0] == 0)
		exits(0);

	/*
	 * lookup
	 */
	secret = findsecret(KEYDB, tr->uid, sbuf);
	if(!getkey(tr->hostid, &hkey) || secret == nil){
		replyerror("chap-fail bad response %s", raddr);
		logfail(tr->uid);
		return;
	}

	/*
	 *  check for match
	 */
	s = md5(&reply.id, 1, 0, 0);
	md5((uint8_t*)secret, strlen(secret), 0, s);
	md5((uint8_t*)chal, sizeof(chal), digest, s);

	if(tsmemcmp(digest, reply.resp, MD5dlen) != 0){
		replyerror("chap-fail bad response %s", raddr);
		logfail(tr->uid);
		return;
	}

	succeed(tr->uid);

	/*
	 *  reply with ticket & authenticator
	 */
	tickauthreply(tr, &hkey);

	syslog(0, AUTHLOG, "chap-ok %s %s", tr->uid, raddr);
}

enum {
	MsvAvEOL = 0,
	MsvAvNbComputerName,
	MsvAvNbDomainName,
	MsvAvDnsComputerName,
	MsvAvDnsomainName,
};

char*
getname(int id, uint8_t *ntblob, int ntbloblen, char *buf, int nbuf)
{
	int aid, alen, i;
	uint8_t *p, *e;
	char *d;
	Rune r;

	d = buf;
	p = ntblob+8+8+8+4;	/* AvPair offset */
	e = ntblob+ntbloblen;
	while(p+4 <= e){
		aid = *p++;
		aid |= *p++ << 8;
		alen = *p++;
		alen |= *p++ << 8;

		if(p+alen > e)
			break;
		if(aid == id){
			for(i=0; i+1 < alen && d-buf < nbuf-(UTFmax+1); i+=2){
				r = p[i] | p[i+1]<<8;
				d += runetochar(d, &r);
			}
			break;
		}
		p += alen;
	}
	*d = '\0';
	return buf;
}

static uint8_t ntblobsig[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void
mschap(Ticketreq *tr)
{
	char *secret;
	char sbuf[SECRETLEN], windom[128];
	uint8_t chal[CHALLEN], ntblob[1024];
	uint8_t hash[MShashlen];
	uint8_t hash2[MShashlen];
	uint8_t resp[MSresplen];
	OMSchapreply reply;
	int dupe, lmok, ntok, ntbloblen;
	DigestState *s;
	uint8_t digest[SHA1dlen];

	/*
	 *  Create a challenge and send it.
	 */
	genrandom(chal, sizeof(chal));
	if(write(1, chal, sizeof(chal)) != sizeof(chal))
		exits(0);

	/*
	 *  get chap reply
	 */
	if(readn(0, &reply, OMSCHAPREPLYLEN) < 0)
		exits(0);

	/*
	 * CIFS/NTLMv2 uses variable length NT response.
	 */
	ntbloblen = 0;
	if(memcmp(reply.NTresp+16, ntblobsig, sizeof(ntblobsig)) == 0){
		/* Version[1], HiVision[1], Z[6] */
		ntbloblen += 1+1+6;
		memmove(ntblob, reply.NTresp+16, ntbloblen);

		/* Time[8], CC[8], Z[4] */
		if(readn(0, ntblob+ntbloblen, 8+8+4) < 0)
			exits(0);
		ntbloblen += 8+8+4;

		/* variable AvPairs */
		for(;;){
			int len, id;

			if(ntbloblen > sizeof(ntblob)-4)
				exits(0);
			/* AvId[2], AvLen[2], Vairable[AvLen] */
			if(readn(0, ntblob+ntbloblen, 4) < 0)
				exits(0);
			id = ntblob[ntbloblen+0] | ntblob[ntbloblen+1]<<8;
			len = ntblob[ntbloblen+2] | ntblob[ntbloblen+3]<<8;
			ntbloblen += 4;

			if(ntbloblen+len > sizeof(ntblob))
				exits(0);
			if(readn(0, ntblob+ntbloblen, len) < 0)
				exits(0);
			ntbloblen += len;
			if(id == MsvAvEOL)
				break;
		}

		/* Z[4] */
		if(ntbloblen > sizeof(ntblob)-4)
			exits(0);
		if(readn(0, ntblob+ntbloblen, 4) < 0)
			exits(0);
		ntbloblen += 4;
	}

	safecpy(tr->uid, reply.uid, sizeof(tr->uid));
	if(tr->uid[0] == 0)
		exits(0);

	/*
	 * lookup
	 */
	secret = findsecret(KEYDB, tr->uid, sbuf);
	if(!getkey(tr->hostid, &hkey) || secret == nil){
		replyerror("mschap-fail bad response %s/%s(%s)", tr->uid, tr->hostid, raddr);
		logfail(tr->uid);
		return;
	}

	if(ntbloblen > 0){
		getname(MsvAvNbDomainName, ntblob, ntbloblen, windom, sizeof(windom));

		for(;;){
			ntv2hash(hash, secret, tr->uid, windom);

			/*
			 * LmResponse = Cat(HMAC_MD5(LmHash, Cat(SC, CC)), CC)
			 */
			s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
			hmac_md5((uint8_t*)reply.LMresp+16, 8, hash, MShashlen, resp, s);
			lmok = tsmemcmp(resp, reply.LMresp, 16) == 0;

			/*
			 * NtResponse = Cat(HMAC_MD5(NtHash, Cat(SC, NtBlob)), NtBlob)
			 */
			s = hmac_md5(chal, 8, hash, MShashlen, nil, nil);
			hmac_md5(ntblob, ntbloblen, hash, MShashlen, resp, s);
			ntok = tsmemcmp(resp, reply.NTresp, 16) == 0;

			if(lmok || ntok || windom[0] == '\0')
				break;

			windom[0] = '\0';	/* try NIL domain */
		}
		dupe = 0;
	} else {
		lmhash(hash, secret);
		mschalresp(resp, hash, chal);
		lmok = tsmemcmp(resp, reply.LMresp, MSresplen) == 0;
		nthash(hash, secret);
		mschalresp(resp, hash, chal);
		ntok = tsmemcmp(resp, reply.NTresp, MSresplen) == 0;
		dupe = tsmemcmp(reply.LMresp, reply.NTresp, MSresplen) == 0;
	}

	/*
	 * It is valid to send the same response in both the LM and NTLM 
	 * fields provided one of them is correct, if neither matches,
	 * or the two fields are different and either fails to match, 
	 * the whole sha-bang fails.
	 *
	 * This is an improvement in security as it allows clients who
	 * wish to do NTLM auth (which is insecure) not to send
	 * LM tokens (which is very insecure).
	 *
	 * Windows servers supports clients doing this also though
	 * windows clients don't seem to use the feature.
	 */
	if((!ntok && !lmok) || ((!ntok || !lmok) && !dupe)){
		replyerror("mschap-fail bad response %s/%s(%s)", tr->uid, tr->hostid, raddr);
		logfail(tr->uid);
		return;
	}

	succeed(tr->uid);

	/*
	 *  reply with ticket & authenticator
	 */
	tickauthreply(tr, &hkey);

	syslog(0, AUTHLOG, "mschap-ok %s/%s(%s)", tr->uid, tr->hostid, raddr);

	nthash(hash, secret);
	md4(hash, 16, hash2, 0);
	s = sha1(hash2, 16, 0, 0);
	sha1(hash2, 16, 0, s);
	sha1(chal, 8, digest, s);

	if(write(1, digest, 16) != 16)
		exits(0);
}

void
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

void
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

void
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

void
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

void
desencrypt(uint8_t data[8], uint8_t key[7])
{
	uint32_t ekey[32];

	key_setup(key, ekey);
	block_cipher(ekey, data, 0);
}

/*
 *  return true of the speaker may speak for the user
 *
 *  a speaker may always speak for himself/herself
 */
int
speaksfor(char *speaker, char *user)
{
	Ndbtuple *tp, *ntp;
	Ndbs s;
	int ok;
	char notuser[Maxpath];

	if(strcmp(speaker, user) == 0)
		return 1;

	if(db == nil)
		return 0;

	tp = ndbsearch(db, &s, "hostid", speaker);
	if(tp == nil)
		return 0;

	ok = 0;
	snprint(notuser, sizeof notuser, "!%s", user);
	for(ntp = tp; ntp != nil; ntp = ntp->entry)
		if(strcmp(ntp->attr, "uid") == 0){
			if(strcmp(ntp->val, notuser) == 0){
				ok = 0;
				break;
			}
			if(*ntp->val == '*' || strcmp(ntp->val, user) == 0)
				ok = 1;
		}
	ndbfree(tp);
	return ok;
}

/*
 *  return an error reply
 */
void
replyerror(char *fmt, ...)
{
	char buf[AERRLEN+1];
	va_list arg;

	memset(buf, 0, sizeof(buf));
	va_start(arg, fmt);
	vseprint(buf + 1, buf + sizeof(buf), fmt, arg);
	va_end(arg);
	buf[AERRLEN] = 0;
	buf[0] = AuthErr;
	write(1, buf, AERRLEN+1);
	syslog(0, AUTHLOG, buf+1);
}

void
getraddr(char *dir)
{
	int n;
	char *cp;
	char file[Maxpath];

	raddr[0] = 0;
	snprint(file, sizeof(file), "%s/remote", dir);
	n = readfile(file, raddr, sizeof(raddr)-1);
	if(n < 0)
		return;
	raddr[n] = 0;

	cp = strchr(raddr, '\n');
	if(cp)
		*cp = 0;
	cp = strchr(raddr, '!');
	if(cp)
		*cp = 0;
}

void
initkeyseed(void)
{
	static char info[] = "PRF key for generation of dummy user keys";
	char k[DESKEYLEN], *u;

	u = getuser();
	if(!finddeskey(KEYDB, u, k)){
		syslog(0, AUTHLOG, "user %s not in keydb", u);
		exits(0);
	}
	hmac_sha2_256((uint8_t*)info, sizeof(info)-1, (uint8_t*)k, sizeof(k), keyseed, nil);
	memset(k, 0, sizeof(k));
}

void
mkkey(Keyslot *k)
{
	uint8_t h[SHA2_256dlen];
	Authkey *a = k;

	genrandom((uint8_t*)a, sizeof(Authkey));

	/*
	 * the DES key has to be constant for a user in each response,
	 * so we make one up pseudo randomly from a keyseed and user name.
	 */
	hmac_sha2_256((uint8_t*)k->id, strlen(k->id), keyseed, sizeof(keyseed), h, nil);
	memmove(a->des, h, DESKEYLEN);
	memset(h, 0, sizeof(h));
}

void
mkticket(Ticketreq *tr, Ticket *t)
{
	memset(t, 0, sizeof(Ticket));
	memmove(t->chal, tr->chal, CHALLEN);
	safecpy(t->cuid, tr->uid, ANAMELEN);
	safecpy(t->suid, tr->uid, ANAMELEN);
	genrandom(t->key, NONCELEN);
	t->form = ticketform;
}

/*
 *  reply with ticket and authenticator
 */
void
tickauthreply(Ticketreq *tr, Authkey *key)
{
	Ticket t;
	Authenticator a;
	char buf[MAXTICKETLEN+MAXAUTHENTLEN+1];
	int n;

	mkticket(tr, &t);
	t.num = AuthTs;
	n = 0;
	buf[n++] = AuthOK;
	n += convT2M(&t, buf+n, sizeof(buf)-n, key);
	memset(&a, 0, sizeof(a));
	memmove(a.chal, t.chal, CHALLEN);
	genrandom(a.rand, NONCELEN);
	a.num = AuthAc;
	n += convA2M(&a, buf+n, sizeof(buf)-n, &t);
	if(write(1, buf, n) != n)
		exits(0);
}

void
safecpy(char *to, char *from, int len)
{
	strncpy(to, from, len);
	to[len-1] = 0;
}

