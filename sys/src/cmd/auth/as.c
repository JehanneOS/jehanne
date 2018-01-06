/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
/*
 * as user cmd [arg...] - run cmd with args as user on this cpu server.
 *	must be hostowner for this to work.
 *	needs #¤/caphash and #¤/capuse.
 */
#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <libsec.h>
#include <auth.h>
#include <authsrv.h>
#include "authcmdlib.h"

int	debug;

int	becomeuser(char*);
void	createuser(void);
void	*emalloc(uint32_t);
void	*erealloc(void*, uint32_t);
void	initcap(void);
int	mkcmd(char*, char*, int);
int	myauth(int, char*);
int	qidcmp(Qid, Qid);
void	runas(char *, char *);
void	usage(void);

#pragma varargck	argpos clog 1
#pragma varargck	argpos fatal 1

static void
fatal(char *fmt, ...)
{
	char msg[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(msg, msg + sizeof msg, fmt, arg);
	va_end(arg);
	error("%s", msg);
}

void
main(int argc, char *argv[])
{
	debug = 0;
	ARGBEGIN{
	case 'd':
		debug = 1;
		break;
	default:
		usage();
	}ARGEND

	initcap();
	if(argc >= 2)
		runas(argv[0], argv[1]);
	else
		usage();
}

void
runas(char *user, char *cmd)
{
	if(becomeuser(user) < 0)
		sysfatal("can't change uid for %s: %r", user);
	putenv(ENV_SERVICE, "rx");
	execl("/bin/rc", "rc", "-lc", cmd, nil);
	sysfatal("exec /bin/rc: %r");
}

void *
emalloc(uint32_t n)
{
	void *p;

	if(p = mallocz(n, 1))
		return p;
	fatal("out of memory");
	return 0;
}

void *
erealloc(void *p, uint32_t n)
{
	if(p = realloc(p, n))
		return p;
	fatal("out of memory");
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [-c] [user] [command]\n", argv0);
	exits("usage");
}

/*
 *  keep caphash fd open since opens of it could be disabled
 */
static int caphashfd;

void
initcap(void)
{
	caphashfd = open("#¤/caphash", OCEXEC|OWRITE);
	if(caphashfd < 0)
		fprint(2, "%s: opening #¤/caphash: %r\n", argv0);
}

/*
 *  create a change uid capability 
 */
char*
mkcap(char *from, char *to)
{
	uint8_t rand[20];
	char *cap;
	char *key;
	int nfrom, nto;
	uint8_t hash[SHA1dlen];

	if(caphashfd < 0)
		return nil;

	/* create the capability */
	nto = strlen(to);
	nfrom = strlen(from);
	cap = emalloc(nfrom+1+nto+1+sizeof(rand)*3+1);
	sprint(cap, "%s@%s", from, to);
	genrandom(rand, sizeof(rand));
	key = cap+nfrom+1+nto+1;
	enc64(key, sizeof(rand)*3, rand, sizeof(rand));

	/* hash the capability */
	hmac_sha1((uint8_t*)cap, strlen(cap), (uint8_t*)key, strlen(key), hash, nil);

	/* give the kernel the hash */
	key[-1] = '@';
	if(write(caphashfd, hash, SHA1dlen) < 0){
		free(cap);
		return nil;
	}

	return cap;
}

int
usecap(char *cap)
{
	int fd, rv;

	fd = open("#¤/capuse", OWRITE);
	if(fd < 0)
		return -1;
	rv = write(fd, cap, strlen(cap));
	close(fd);
	return rv;
}

int
becomeuser(char *new)
{
	char *cap;
	int rv;

	cap = mkcap(getuser(), new);
	if(cap == nil)
		return -1;
	rv = usecap(cap);
	free(cap);

	newns(new, nil);
	return rv;
}
