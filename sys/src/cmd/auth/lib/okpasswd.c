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

char *trivial[] = {
	"login",
	"guest",
	"change me",
	"passwd",
	"no passwd",
	"anonymous",
	0
};

char*
okpasswd(char *p)
{
	char passwd[ANAMELEN];
	char back[ANAMELEN];
	int i, n;

	strncpy(passwd, p, sizeof passwd - 1);
	passwd[sizeof passwd - 1] = '\0';
	n = strlen(passwd);
	while(passwd[n - 1] == ' ')
		n--;
	passwd[n] = '\0';
	for(i = 0; i < n; i++)
		back[i] = passwd[n - 1 - i];
	back[n] = '\0';
	if(n < 8)
		return "password must be at least 8 chars";

	for(i = 0; trivial[i]; i++)
		if(strcmp(passwd, trivial[i]) == 0
		|| strcmp(back, trivial[i]) == 0)
			return "trivial password";

	return 0;
}
