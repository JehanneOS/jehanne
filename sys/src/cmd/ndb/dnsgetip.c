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
/* one-shot resolver */
#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

Cfg cfg;
char *dbfile;
int debug		= 0;
char *logfile		= "dnsgetip";
int	maxage		= 60*60;
char mntpt[Maxpath];
int	needrefresh	= 0;
uint32_t	now		= 0;
int64_t	nowns		= 0;
int	testing		= 0;
int	traceactivity	= 0;
char	*zonerefreshprogram;

int aflag;

void
resolve(char *name, int type)
{
	Request req;
	RR *rp;

	memset(&req, 0, sizeof req);
	getactivity(&req, 0);
	req.isslave = 1;
	req.aborttime = NS2MS(nowns) + Maxreqtm;

	rp = dnresolve(name, Cin, type, &req, 0, 0, Recurse, 0, 0);
	rrremneg(&rp);
	while(rp != nil){
		print("%s\n", rp->ip->name);
		if(!aflag)
			exits(nil);
		rp = rp->next;
	}
}

void
usage(void)
{
	fprint(2, "%s: [-adx] domain\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	strcpy(mntpt, "/net");
	cfg.inside = 1;
	cfg.resolver = 1;

	ARGBEGIN{
	case 'a':
		aflag = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'x':
		strcpy(mntpt, "/net.alt");
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	if(strcmp(ipattr(*argv), "ip") == 0)
		print("%s\n", *argv);
	else {
		dninit();
		resolve(*argv, Ta);
		resolve(*argv, Taaaa);
	}
	exits(nil);
}

RR*
getdnsservers(int class)
{
	return dnsservers(class);
}

/* stubs */
void syslog(int _1, const char* _2, const char* _3, ...){}
void logreply(int _1, uint8_t* _2, DNSmsg* _3){}
void logsend(int _1, int _2, uint8_t* _3, char* _4, char* _5, int _6){}
