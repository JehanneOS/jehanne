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
#include <authsrv.h>
#include "authcmdlib.h"

/*
 * print a key in des standard form
 */
int
deskeyfmt(Fmt *f)
{
	uint8_t key[8];
	char buf[32];
	uint8_t *k;
	int i;

	k = va_arg(f->args, uint8_t*);
	key[0] = 0;
	for(i = 0; i < 7; i++){
		key[i] |= k[i] >> i;
		key[i] &= ~1;
		key[i+1] = k[i] << (7 - i);
	}
	key[7] &= ~1;
	sprint(buf, "%.3uo %.3uo %.3uo %.3uo %.3uo %.3uo %.3uo %.3uo",
		key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
	fmtstrcpy(f, buf);
	return 0;
}
