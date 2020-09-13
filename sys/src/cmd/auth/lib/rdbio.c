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
#include <bio.h>
#include <chartypes.h>
#include <authsrv.h>
#include "authcmdlib.h"

void
clrbio(Acctbio *a)
{
	int i;

	if(a->user)
		free(a->user);
	if(a->postid)
		free(a->postid);
	if(a->name)
		free(a->name);
	if(a->dept)
		free(a->dept);
	for(i = 0; i < Nemail; i++)
		if(a->email[i])
			free(a->email[i]);
	memset(a, 0, sizeof(Acctbio));
}

void
rdbio(char *file, char *user, Acctbio *a)
{
	int i,n;
	Biobuf *b;
	char *p;
	char *field[20];

	memset(a, 0, sizeof(Acctbio));
	b = Bopen(file, OREAD);
	if(b != 0){
		while(p = Brdline(b, '\n')){
			p[Blinelen(b)-1] = 0;
			n = getfields(p, field, nelem(field), 0, "|");
			if(n < 4)
				continue;
			if(strcmp(field[0], user) != 0)
				continue;

			clrbio(a);

			a->postid = strdup(field[1]);
			a->name = strdup(field[2]);
			a->dept = strdup(field[3]);
			if(n-4 >= Nemail)
				n = Nemail-4;
			for(i = 4; i < n; i++)
				a->email[i-4] = strdup(field[i]);
		}
		Bterm(b);
	}
	a->user = strdup(user);
}
