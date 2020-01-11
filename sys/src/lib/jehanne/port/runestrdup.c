/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

Rune*
jehanne_runestrdup(const Rune *s)
{  
	Rune *ns;

	ns = jehanne_malloc(sizeof(Rune)*(jehanne_runestrlen(s) + 1));
	if(ns == 0)
		return 0;
	jehanne_setmalloctag(ns, jehanne_getcallerpc());

	return jehanne_runestrcpy(ns, s);
}
