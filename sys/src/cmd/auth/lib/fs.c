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
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

Fs fs[3] =
{
[Plan9]		{ "/mnt/keys", "plan 9 key", "/adm/keys.who", 0 },
[Securenet]	{ "/mnt/netkeys", "network access key", "/adm/netkeys.who", 0 },
};
