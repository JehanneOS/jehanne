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
#include <auth.h>
#include "authlocal.h"

int
amount(int fd, char *mntpt, int flags, char *aname)
{
	int rv, afd;
	AuthInfo *ai;

	afd = sys_fauth(fd, aname);
	if(afd >= 0){
		ai = auth_proxy(afd, amount_getkey, "proto=p9any role=client");
		if(ai != nil)
			auth_freeAI(ai);
	}
	rv = sys_mount(fd, afd, mntpt, flags, aname, '9');
	if(afd >= 0)
		sys_close(afd);
	return rv;
}
