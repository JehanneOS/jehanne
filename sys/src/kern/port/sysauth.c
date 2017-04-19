/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<authsrv.h>

char	*eve;
char	hostdomain[DOMLEN];

/*
 *  return true if current user is eve
 */
int
iseve(void)
{
	return jehanne_strcmp(eve, up->user) == 0;
}

int
isevegroup(void)
{
	return ingroup(up->user, eve);
}

int
sysfversion(int fd, int msize, char *version, int nversion)
{
	Chan *c;
	int result;

	version = validaddr(version, nversion, 1);
	/* check there's a NUL in the version string */
	if(nversion == 0 || jehanne_memchr(version, 0, nversion) == nil)
		error(Ebadarg);

	c = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	result = mntversion(c, msize, version, nversion);

	cclose(c);
	poperror();

	return result;
}

int
sysfauth(int fd, char *aname)
{
	Chan *c, *ac;

	aname = validaddr(aname, 1, 0);
	aname = validnamedup(aname, 1);
	if(waserror()){
		jehanne_free(aname);
		nexterror();
	}
	c = fdtochan(fd, ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	ac = mntauth(c, aname);
	/* at this point ac is responsible for keeping c alive */
	cclose(c);
	poperror();	/* c */
	jehanne_free(aname);
	poperror();	/* aname */

	if(waserror()){
		cclose(ac);
		nexterror();
	}

	fd = newfd(ac);
	if(fd < 0)
		error(Enofd);
	poperror();	/* ac */

	/* always mark it close on exec */
	ac->flag |= CCEXEC;

	return fd;
}

/*
 *  called by devcons() for user device
 *
 *  anyone can become none
 */
long
userwrite(char* a, long n)
{
	if(n != 4 || jehanne_strncmp(a, "none", 4) != 0)
		error(Eperm);
	kstrdup(&up->user, "none");
	up->basepri = PriNormal;

	return n;
}

/*
 *  called by devcons() for host owner/domain
 *
 *  writing hostowner also sets user
 */
long
hostownerwrite(char* a, long n)
{
	char buf[128];

	if(!iseve())
		error(Eperm);
	if(n <= 0 || n >= sizeof buf)
		error(Ebadarg);
	jehanne_memmove(buf, a, n);
	buf[n] = 0;

	renameuser(eve, buf);
	srvrenameuser(eve, buf);
	shrrenameuser(eve, buf);
	kstrdup(&eve, buf);
	kstrdup(&up->user, buf);
	up->basepri = PriNormal;

	return n;
}

long
hostdomainwrite(char* a, long n)
{
	char buf[DOMLEN];

	if(!iseve())
		error(Eperm);
	if(n >= DOMLEN)
		error(Ebadarg);
	jehanne_memset(buf, 0, DOMLEN);
	jehanne_strncpy(buf, a, n);
	if(buf[0] == 0)
		error(Ebadarg);
	jehanne_memmove(hostdomain, buf, DOMLEN);

	return n;
}
