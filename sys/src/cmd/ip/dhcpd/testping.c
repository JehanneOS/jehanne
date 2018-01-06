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
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "dat.h"

char	*blog = "ipboot";

void
main(int argc, char **argv)
{
	fmtinstall('E', eipconv);
	fmtinstall('I', eipconv);

	if(argc < 2)
		exits(0);
	if(icmpecho(argv[1]))
		fprint(2, "%s live\n", argv[1]);
	else
		fprint(2, "%s doesn't answer\n", argv[1]);
}
