/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>

static Lock fmtl;

void
_fmtlock(void)
{
	jehanne_lock(&fmtl);
}

void
_fmtunlock(void)
{
	jehanne_unlock(&fmtl);
}

int
_efgfmt(Fmt* f)
{
	return -1;
}
