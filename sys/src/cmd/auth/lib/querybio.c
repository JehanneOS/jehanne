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


#define TABLEN 8

static char*
defreadln(char *prompt, char *def, int must, int *changed)
{
	char pr[512];
	char reply[256];

	do {
		if(def && *def){
			if(must)
				snprint(pr, sizeof pr, "%s[return = %s]: ", prompt, def);
			else
				snprint(pr, sizeof pr, "%s[return = %s, space = none]: ", prompt, def);
		} else
			snprint(pr, sizeof pr, "%s: ", prompt);
		readln(pr, reply, sizeof(reply), 0);
		switch(*reply){
		case ' ':
			break;
		case 0:
			return def;
		default:
			*changed = 1;
			if(def)
				free(def);
			return strdup(reply);
		}
	} while(must);

	if(def){
		*changed = 1;
		free(def);
	}
	return 0;
}

/*
 *  get bio from stdin
 */
int
querybio(char *file, char *user, Acctbio *a)
{
	int i;
	int changed;

	rdbio(file, user, a);
	a->postid = defreadln("Post id", a->postid, 0, &changed);
	a->name = defreadln("User's full name", a->name, 1, &changed);
	a->dept = defreadln("Department #", a->dept, 1, &changed);
	a->email[0] = defreadln("User's email address", a->email[0], 1, &changed);
	a->email[1] = defreadln("Sponsor's email address", a->email[1], 0, &changed);
	for(i = 2; i < Nemail; i++){
		if(a->email[i-1] == 0)
			break;
		a->email[i] = defreadln("other email address", a->email[i], 0, &changed);
	}
	return changed;
}
