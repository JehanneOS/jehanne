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

#include <u.h>
#include <lib9.h>
#include <auth.h>

/*
 *  become the authenticated user
 */
int
auth_chuid(AuthInfo *ai, char *ns)
{
	int rv, fd;

	if(ai == nil || ai->cap == nil || ai->cap[0] == 0){
		werrstr("no capability");
		return -1;
	}

	/* change uid */
	fd = sys_open("#¤/capuse", OWRITE);
	if(fd < 0){
		werrstr("opening #¤/capuse: %r");
		return -1;
	}
	rv = jehanne_write(fd, ai->cap, strlen(ai->cap));
	sys_close(fd);
	if(rv < 0){
		werrstr("writing %s to #¤/capuse: %r", ai->cap);
		return -1;
	}

	/* get a link to factotum as new user */
	fd = sys_open("/srv/factotum", ORDWR);
	if(fd >= 0)
		sys_mount(fd, -1, "/mnt", MREPL, "", '9');

	/* set up new namespace */
	return newns(ai->cuid, ns);
}
