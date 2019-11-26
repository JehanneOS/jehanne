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
#include <u.h>
#include <lib9.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

/* get a notification from another system of a changed zone */
void
dnnotify(DNSmsg *reqp, DNSmsg *repp, Request * _1)
{
	RR *tp;
	Area *a;

	/* move one question from reqp to repp */
	memset(repp, 0, sizeof(*repp));
	tp = reqp->qd;
	reqp->qd = tp->next;
	tp->next = 0;
	repp->qd = tp;
	repp->id = reqp->id;
	repp->flags = Fresp  | Onotify | Fauth;

	/* anything to do? */
	if(zonerefreshprogram == nil)
		return;

	/* make sure its the right type */
	if(repp->qd->type != Tsoa)
		return;

	dnslog("notification for %s", repp->qd->owner->name);

	/* is it something we care about? */
	a = inmyarea(repp->qd->owner->name);
	if(a == nil)
		return;

	dnslog("serial old %lud new %lud", a->soarr->soa->serial,
		repp->qd->soa->serial);

	/* do nothing if it didn't change */
	if(a->soarr->soa->serial != repp->qd->soa->serial)
		a->needrefresh = 1;
}

/* notify a slave that an area has changed. */
static void
send_notify(char *slave, RR *soa, Request *req)
{
	int i, len, n, reqno, status, fd;
	char *err;
	uint8_t ibuf[Maxudp+Udphdrsize], obuf[Maxudp+Udphdrsize];
	RR *rp;
	Udphdr *up = (Udphdr*)obuf;
	DNSmsg repmsg;

	/* create the request */
	reqno = rand();
	n = mkreq(soa->owner, Cin, obuf, Fauth | Onotify, reqno);

	/* get an address */
	if(strcmp(ipattr(slave), "ip") == 0) {
		if (parseip(up->raddr, slave) == -1)
			dnslog("bad address %s to notify", slave);
	} else {
		rp = dnresolve(slave, Cin, Ta, req, nil, 0, 1, 1, &status);
		if(rp == nil)
			rp = dnresolve(slave, Cin, Taaaa, req, nil, 0, 1, 1, &status);
		if(rp == nil)
			return;
		parseip(up->raddr, rp->ip->name);
		rrfreelist(rp);		/* was rrfree */
	}

	fd = udpport(nil);
	if(fd < 0)
		return;

	/* send 3 times or until we get anything back */
	n += Udphdrsize;
	for(i = 0; i < 3; i++, freeanswers(&repmsg)){
		dnslog("sending %d byte notify to %s/%I.%d about %s", n, slave,
			up->raddr, nhgets(up->rport), soa->owner->name);
		memset(&repmsg, 0, sizeof repmsg);
		if(jehanne_write(fd, obuf, n) != n)
			break;
		sys_alarm(2*1000);
		len = jehanne_read(fd, ibuf, sizeof ibuf);
		sys_alarm(0);
		if(len <= Udphdrsize)
			continue;
		err = convM2DNS(&ibuf[Udphdrsize], len, &repmsg, nil);
		if(err != nil) {
			free(err);
			continue;
		}
		if(repmsg.id == reqno && (repmsg.flags & Omask) == Onotify)
			break;
	}
	if (i < 3)
		freeanswers(&repmsg);
	sys_close(fd);
}

/* send notifies for any updated areas */
static void
notify_areas(Area *a, Request *req)
{
	Server *s;

	for(; a != nil; a = a->next){
		if(!a->neednotify)
			continue;

		/* send notifies to all slaves */
		for(s = a->soarr->soa->slaves; s != nil; s = s->next)
			send_notify(s->name, a->soarr, req);
		a->neednotify = 0;
	}
}

/*
 *  process to notify other servers of changes
 *  (also reads in new databases)
 */
void
notifyproc(void)
{
	Request req;

	switch(sys_rfork(RFPROC|RFNOTEG|RFMEM|RFNOWAIT)){
	case -1:
		return;
	case 0:
		break;
	default:
		return;
	}

	procsetname("notify slaves");
	memset(&req, 0, sizeof req);
	req.isslave = 1;	/* don't fork off subprocesses */

	for(;;){
		getactivity(&req, 0);
		notify_areas(owned, &req);
		putactivity(0);
		sleep(60*1000);
	}
}
