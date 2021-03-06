/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <chartypes.h>

static int	nettrans(const char*, char*, int na, char*, int);

enum
{
	Maxpath=	256,
};

/*
 *  announce a network service.
 */
int
jehanne_announce(const char *addr, char *dir)
{
	int ctl, n, m;
	char buf[Maxpath];
	char buf2[Maxpath];
	char netdir[Maxpath];
	char naddr[Maxpath];
	char *cp;

	/*
	 *  translate the address
	 */
	if(nettrans(addr, naddr, sizeof(naddr), netdir, sizeof(netdir)) < 0)
		return -1;

	/*
	 * get a control channel
	 */
	ctl = sys_open(netdir, ORDWR);
	if(ctl<0){
		jehanne_werrstr("announce opening %s: %r", netdir);
		return -1;
	}
	cp = jehanne_strrchr(netdir, '/');
	if(cp == nil){
		jehanne_werrstr("announce arg format %s", netdir);
		sys_close(ctl);
		return -1;
	}
	*cp = 0;

	/*
	 *  find out which line we have
	 */
	n = jehanne_snprint(buf, sizeof(buf), "%s/", netdir);
	m = jehanne_read(ctl, &buf[n], sizeof(buf)-n-1);
	if(m <= 0){
		jehanne_werrstr("announce reading %s: %r", netdir);
		sys_close(ctl);
		return -1;
	}
	buf[n+m] = 0;

	/*
	 *  make the call
	 */
	n = jehanne_snprint(buf2, sizeof(buf2), "announce %s", naddr);
	if(jehanne_write(ctl, buf2, n)!=n){
		jehanne_werrstr("announce writing %s: %r", netdir);
		sys_close(ctl);
		return -1;
	}

	/*
	 *  return directory etc.
	 */
	if(dir){
		jehanne_strncpy(dir, buf, NETPATHLEN);
		dir[NETPATHLEN-1] = 0;
	}
	return ctl;
}

/*
 *  listen for an incoming call
 */
int
jehanne_listen(const char *dir, char *newdir)
{
	int ctl, n, m;
	char buf[Maxpath];
	char *cp;

	/*
	 *  open listen, wait for a call
	 */
	jehanne_snprint(buf, sizeof(buf), "%s/listen", dir);
	ctl = sys_open(buf, ORDWR);
	if(ctl < 0){
		jehanne_werrstr("listen opening %s: %r", buf);
		return -1;
	}

	/*
	 *  find out which line we have
	 */
	jehanne_strncpy(buf, dir, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	cp = jehanne_strrchr(buf, '/');
	if(cp == nil){
		sys_close(ctl);
		jehanne_werrstr("listen arg format %s", dir);
		return -1;
	}
	*++cp = 0;
	n = cp-buf;
	m = jehanne_read(ctl, cp, sizeof(buf) - n - 1);
	if(m <= 0){
		sys_close(ctl);
		jehanne_werrstr("listen reading %s/listen: %r", dir);
		return -1;
	}
	buf[n+m] = 0;

	/*
	 *  return directory etc.
	 */
	if(newdir){
		jehanne_strncpy(newdir, buf, NETPATHLEN);
		newdir[NETPATHLEN-1] = 0;
	}
	return ctl;

}

/*
 *  accept a call, return an fd to the open data file
 */
int
jehanne_accept(int ctl, const char *dir)
{
	char buf[Maxpath];
	const char *num;
	int32_t n;

	num = jehanne_strrchr(dir, '/');
	if(num == nil)
		num = dir;
	else
		num++;

	n = jehanne_snprint(buf, sizeof(buf), "accept %s", num);
	jehanne_write(ctl, buf, n); /* ignore return value, network might not need accepts */

	jehanne_snprint(buf, sizeof(buf), "%s/data", dir);
	return sys_open(buf, ORDWR);
}

/*
 *  reject a call, tell device the reason for the rejection
 */
int
jehanne_reject(int ctl, const char *dir, const char *cause)
{
	char buf[Maxpath];
	const char *num;
	int32_t n;

	num = jehanne_strrchr(dir, '/');
	if(num == 0)
		num = dir;
	else
		num++;
	jehanne_snprint(buf, sizeof(buf), "reject %s %s", num, cause);
	n = jehanne_strlen(buf);
	if(jehanne_write(ctl, buf, n) != n)
		return -1;
	return 0;
}

/*
 *  perform the identity translation (in case we can't reach cs)
 */
static int
identtrans(char *netdir, const char *addr, char *naddr, int na,
	   char *file, int nf)
{
	char proto[Maxpath];
	char *p;

	USED(nf);

	/* parse the protocol */
	jehanne_strncpy(proto, addr, sizeof(proto));
	proto[sizeof(proto)-1] = 0;
	p = jehanne_strchr(proto, '!');
	if(p)
		*p++ = 0;

	jehanne_snprint(file, nf, "%s/%s/clone", netdir, proto);
	jehanne_strncpy(naddr, p, na);
	naddr[na-1] = 0;

	return 1;
}

/*
 *  call up the connection server and get a translation
 */
static int
nettrans(const char *addr, char *naddr, int na, char *file, int nf)
{
	int i, fd;
	char buf[Maxpath];
	char netdir[Maxpath];
	char *p, *p2;
	int32_t n;

	/*
	 *  parse, get network directory
	 */
	p = jehanne_strchr(addr, '!');
	if(p == 0){
		jehanne_werrstr("bad dial string: %s", addr);
		return -1;
	}
	if(*addr != '/'){
		jehanne_strncpy(netdir, "/net", sizeof(netdir));
		netdir[sizeof(netdir) - 1] = 0;
	} else {
		for(p2 = p; *p2 != '/'; p2--)
			;
		i = p2 - addr;
		if(i == 0 || i >= sizeof(netdir)){
			jehanne_werrstr("bad dial string: %s", addr);
			return -1;
		}
		jehanne_strncpy(netdir, addr, i);
		netdir[i] = 0;
		addr = p2 + 1;
	}

	/*
	 *  ask the connection server
	 */
	jehanne_snprint(buf, sizeof(buf), "%s/cs", netdir);
	fd = sys_open(buf, ORDWR);
	if(fd < 0)
		return identtrans(netdir, addr, naddr, na, file, nf);
	if(jehanne_write(fd, addr, jehanne_strlen(addr)) < 0){
		sys_close(fd);
		return -1;
	}
	sys_seek(fd, 0, 0);
	n = jehanne_read(fd, buf, sizeof(buf)-1);
	sys_close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;

	/*
	 *  parse the reply
	 */
	p = jehanne_strchr(buf, ' ');
	if(p == 0)
		return -1;
	*p++ = 0;
	jehanne_strncpy(naddr, p, na);
	naddr[na-1] = 0;

	if(buf[0] == '/'){
		p = jehanne_strchr(buf+1, '/');
		if(p == nil)
			p = buf;
		else 
			p++;
	}
	jehanne_snprint(file, nf, "%s/%s", netdir, p);
	return 0;
}
