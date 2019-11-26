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
#include "../icmp.h"

static void
catch(void *a, char *msg)
{
	USED(a);
	if(strstr(msg, "alarm"))
		sys_noted(NCONT);
	else
		sys_noted(NDFLT);
}

#define MSG "dhcp probe"

/*
 *  make sure noone is using the address
 *  TODO: ipv6 ping
 */
int
icmpecho(uint8_t *a)
{
	int fd, i, n, len, rv;
	uint16_t sseq, x;
	char buf[512];
	Icmphdr *ip;

	rv = 0;
	if (!isv4(a))
		return 0;
	sprint(buf, "%I", a);
	fd = dial(netmkaddr(buf, "icmp", "1"), 0, 0, 0);
	if(fd < 0){
		return 0;
	}

	sseq = getpid()*time(0);

	ip = (Icmphdr *)(buf + IPV4HDR_LEN);
	sys_notify(catch);
	for(i = 0; i < 3; i++){
		ip->type = EchoRequest;
		ip->code = 0;
		strcpy((char*)ip->data, MSG);
		ip->seq[0] = sseq;
		ip->seq[1] = sseq>>8;
		len = IPV4HDR_LEN + ICMP_HDRSIZE + sizeof(MSG);

		/* send a request */
		if(jehanne_write(fd, buf, len) < len)
			break;

		/* wait 1/10th second for a reply and try again */
		sys_alarm(100);
		n = jehanne_read(fd, buf, sizeof(buf));
		sys_alarm(0);
		if(n <= 0)
			continue;

		/* an answer to our echo request? */
		x = (ip->seq[1]<<8) | ip->seq[0];
		if(n >= len && ip->type == EchoReply && x == sseq &&
		    strcmp((char*)ip->data, MSG) == 0){
			rv = 1;
			break;
		}
	}
	sys_close(fd);
	return rv;
}
