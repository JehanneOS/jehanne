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
#include <authsrv.h>
#include <bio.h>
#include "authcmdlib.h"

static int
getkey(Authkey *authkey)
{
	Nvrsafe safe;

	if(readnvram(&safe, 0) < 0)
		return -1;
	memmove(authkey->des, safe.machkey, DESKEYLEN);
	memmove(authkey->aes, safe.aesmachkey, AESKEYLEN);
	memset(&safe, 0, sizeof safe);
	return 0;
}

int
getauthkey(Authkey *authkey)
{
	memset(authkey, 0, sizeof(Authkey));
	if(getkey(authkey) == 0)
		return 1;
	print("can't read NVRAM, please enter machine key\n");
	getpass(authkey, nil, 0, 1);
	return 1;
}
