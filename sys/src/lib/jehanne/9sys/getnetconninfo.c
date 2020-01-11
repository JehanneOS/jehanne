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

static char *unknown = "???";

static void
getendpoint(char *dir, char *file, char **sysp, char **servp)
{
	int fd, n;
	char buf[128];
	char *sys, *serv;

	sys = serv = 0;

	jehanne_snprint(buf, sizeof buf, "%s/%s", dir, file);
	fd = sys_open(buf, OREAD);
	if(fd >= 0){
		n = jehanne_read(fd, buf, sizeof(buf)-1);
		if(n>0){
			buf[n-1] = 0;
			serv = jehanne_strchr(buf, '!');
			if(serv){
				*serv++ = 0;
				serv = jehanne_strdup(serv);
			}
			sys = jehanne_strdup(buf);
		}
		sys_close(fd);
	}
	if(serv == 0)
		serv = unknown;
	if(sys == 0)
		sys = unknown;
	*servp = serv;
	*sysp = sys;
}

NetConnInfo*
jehanne_getnetconninfo(const char *dir, int fd)
{
	NetConnInfo *nci;
	char *cp;
	Dir *d;
	char spec[10];
	char path[128];
	char netname[128], *p;

	/* get a directory address via fd */
	if(dir == nil || *dir == 0){
		if(sys_fd2path(fd, path, sizeof(path)) < 0)
			return nil;
		cp = jehanne_strrchr(path, '/');
		if(cp == nil)
			return nil;
		*cp = 0;
		dir = path;
	}

	nci = jehanne_mallocz(sizeof *nci, 1);
	if(nci == nil)
		return nil;

	/* copy connection directory */
	nci->dir = jehanne_strdup(dir);
	if(nci->dir == nil)
		goto err;

	/* get netroot */
	nci->root = jehanne_strdup(dir);
	if(nci->root == nil)
		goto err;
	cp = jehanne_strchr(nci->root+1, '/');
	if(cp == nil)
		goto err;
	*cp = 0;

	/* figure out bind spec */
	d = jehanne_dirstat(nci->dir);
	if(d != nil){
		jehanne_sprint(spec, "#%C%d", d->type, d->dev);
		nci->spec = jehanne_strdup(spec);
	}
	if(nci->spec == nil)
		nci->spec = unknown;
	jehanne_free(d);

	/* get the two end points */
	getendpoint(nci->dir, "local", &nci->lsys, &nci->lserv);
	if(nci->lsys == nil || nci->lserv == nil)
		goto err;
	getendpoint(nci->dir, "remote", &nci->rsys, &nci->rserv);
	if(nci->rsys == nil || nci->rserv == nil)
		goto err;

	jehanne_strecpy(netname, netname+sizeof netname, nci->dir);
	if((p = jehanne_strrchr(netname, '/')) != nil)
		*p = 0;
	if(jehanne_strncmp(netname, "/net/", 5) == 0)
		jehanne_memmove(netname, netname+5, jehanne_strlen(netname+5)+1);
	nci->laddr = jehanne_smprint("%s!%s!%s", netname, nci->lsys, nci->lserv);
	nci->raddr = jehanne_smprint("%s!%s!%s", netname, nci->rsys, nci->rserv);
	if(nci->laddr == nil || nci->raddr == nil)
		goto err;
	return nci;
err:
	jehanne_freenetconninfo(nci);
	return nil;
}

static void
xfree(char *x)
{
	if(x == nil || x == unknown)
		return;
	jehanne_free(x);
}

void
jehanne_freenetconninfo(NetConnInfo *nci)
{
	if(nci == nil)
		return;
	xfree(nci->root);
	xfree(nci->dir);
	xfree(nci->spec);
	xfree(nci->lsys);
	xfree(nci->lserv);
	xfree(nci->rsys);
	xfree(nci->rserv);
	xfree(nci->laddr);
	xfree(nci->raddr);
	jehanne_free(nci);
}
