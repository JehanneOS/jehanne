/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include <ip.h>

#include "boot.h"

static	uint8_t	fsip[IPaddrlen];
	uint8_t	auip[IPaddrlen];
static	char	mpoint[32];

static int isvalidip(uint8_t*);
static void netndb(char*, uint8_t*);
static void netenv(char*, uint8_t*);


void
configip(int bargc, char **bargv, int needfs)
{
	Waitmsg *w;
	int argc, pid;
	char **arg, **argv, buf[32], *p;

	jehanne_fmtinstall('I', eipfmt);
	jehanne_fmtinstall('M', eipfmt);
	jehanne_fmtinstall('E', eipfmt);

	arg = jehanne_malloc((bargc+1) * sizeof(char*));
	if(arg == nil)
		fatal("malloc");
	jehanne_memmove(arg, bargv, bargc * sizeof(char*));
	arg[bargc] = 0;

	buf[0] = 0; /* no default for outin */

jehanne_print("ipconfig...");
	argc = bargc;
	argv = arg;
	jehanne_strcpy(mpoint, "/net");
	ARGBEGIN {
	case 'x':
		p = ARGF();
		if(p != nil)
			jehanne_snprint(mpoint, sizeof(mpoint), "/net%s", p);
		break;
	case 'g':
	case 'b':
	case 'h':
	case 'm':
		p = ARGF();
		USED(p);
		break;
	} ARGEND;

	/* bind in an ip interface */
	if(sys_bind("#I", mpoint, MAFTER) < 0)
		fatal("bind #I\n");
	if(jehanne_access(ipconfigPath, AEXEC) < 0)
		fatal("cannot access ipconfig");

	if(jehanne_access("#l0", AEXIST) == 0 && sys_bind("#l0", mpoint, MAFTER) < 0)
		jehanne_print("bind #l0: %r\n");
	if(jehanne_access("#l1", AEXIST) == 0 && sys_bind("#l1", mpoint, MAFTER) < 0)
		jehanne_print("bind #l1: %r\n");
	if(jehanne_access("#l2", AEXIST) == 0 && sys_bind("#l2", mpoint, MAFTER) < 0)
		jehanne_print("bind #l2: %r\n");
	if(jehanne_access("#l3", AEXIST) == 0 && sys_bind("#l3", mpoint, MAFTER) < 0)
		jehanne_print("bind #l3: %r\n");
	jehanne_werrstr("");

	/* let ipconfig configure the ip interface */
	switch(pid = jehanne_fork()){
	case -1:
		fatal("fork configuring ip");
	case 0:
		sys_exec(ipconfigPath, (const char**)arg);
		fatal("execing ipconfig");
	default:
		break;
	}

	/* wait for ipconfig to finish */
	for(;;){
		w = jehanne_wait();
		if(w != nil && w->pid == pid){
			if(w->msg[0] != 0)
				fatal(w->msg);
			jehanne_free(w);
			break;
		} else if(w == nil)
			fatal("configuring ip");
		jehanne_free(w);
	}

	if(!needfs)
		return;

	/* if we didn't get a file and auth server, query user */
	netndb("fs", fsip);
	if(!isvalidip(fsip))
		netenv("fs", fsip);
	while(!isvalidip(fsip)){
		outin("filesystem IP address", buf, sizeof(buf));
		if (parseip(fsip, buf) == -1)
			jehanne_fprint(2, "configip: can't parse fs ip %s\n", buf);
	}

	netndb("auth", auip);
	if(!isvalidip(auip))
		netenv("auth", auip);
	while(!isvalidip(auip)){
		outin("authentication server IP address", buf, sizeof(buf));
		if (parseip(auip, buf) == -1)
			jehanne_fprint(2, "configip: can't parse auth ip %s\n", buf);
	}
	jehanne_free(arg);
}

static void
setauthaddr(char *proto, int port)
{
	char buf[128];

	jehanne_snprint(buf, sizeof buf, "%s!%I!%d", proto, auip, port);
	authaddr = jehanne_strdup(buf);
}

void
configtcp(Method* m)
{
	configip(bargc, bargv, 1);
	setauthaddr("tcp", 567);
}

int
connecttcp(void)
{
	int fd;
	char buf[64];

	jehanne_snprint(buf, sizeof buf, "tcp!%I!5640", fsip);
	fd = jehanne_dial(buf, 0, 0, 0);
	if (fd < 0)
		jehanne_werrstr("dial %s: %r", buf);
	return fd;
}

static int
isvalidip(uint8_t *ip)
{
	if(ipcmp(ip, IPnoaddr) == 0)
		return 0;
	if(ipcmp(ip, v4prefix) == 0)
		return 0;
	return 1;
}

static void
netenv(char *attr, uint8_t *ip)
{
	int fd, n;
	char buf[128];

	ipmove(ip, IPnoaddr);
	jehanne_snprint(buf, sizeof(buf), "#ec/%s", attr);
	fd = sys_open(buf, OREAD);
	if(fd < 0)
		return;

	n = jehanne_read(fd, buf, sizeof(buf)-1);
	if(n > 0){
		buf[n] = 0;
		if (parseip(ip, buf) == -1)
			jehanne_fprint(2, "netenv: can't parse ip %s\n", buf);
	}
	sys_close(fd);
}

static void
netndb(char *attr, uint8_t *ip)
{
	int fd, n, c;
	char buf[1024];
	char *p;

	ipmove(ip, IPnoaddr);
	jehanne_snprint(buf, sizeof(buf), "%s/ndb", mpoint);
	fd = sys_open(buf, OREAD);
	if(fd < 0)
		return;
	n = jehanne_read(fd, buf, sizeof(buf)-1);
	sys_close(fd);
	if(n <= 0)
		return;
	buf[n] = 0;
	n = jehanne_strlen(attr);
	for(p = buf; ; p++){
		p = jehanne_strstr(p, attr);
		if(p == nil)
			break;
		c = *(p-1);
		if(*(p + n) == '=' && (p == buf || c == '\n' || c == ' ' || c == '\t')){
			p += n+1;
			if (parseip(ip, p) == -1)
				jehanne_fprint(2, "netndb: can't parse ip %s\n", p);
			return;
		}
	}
	return;
}
