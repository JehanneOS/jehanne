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
#include <ndb.h>
#include <ip.h>

/*
 *  search the database for matches
 */

void
usage(void)
{
	fprint(2, "usage: ipquery attr value rattribute\n");
	exits("usage");
}

void
search(Ndb *db, char *attr, char *val, char **rattr, int nrattr)
{
	Ndbtuple *t, *tt;

	tt = ndbipinfo(db, attr, val, rattr, nrattr);
	for(t = tt; t; t = t->entry)
		print("%s=%s ", t->attr, t->val);
	print("\n");
	ndbfree(tt);
}

void
main(int argc, char **argv)
{
	Ndb *db;
	char *dbfile = 0;

	ARGBEGIN{
	case 'f':
		dbfile = ARGF();
		break;
	}ARGEND;

	if(argc < 3)
		usage();

	db = ndbopen(dbfile);
	if(db == 0){
		fprint(2, "no db files\n");
		exits("no db");
	}
	search(db, argv[0], argv[1], argv+2, argc-2);
	ndbclose(db);

	exits(0);
}
