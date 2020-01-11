/*
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

void
jehanne_lock(Lock *l)
{
	if(jehanne_ainc(&l->key) == 1)
		return;	/* changed from 0 -> 1: we hold lock */
	/* otherwise wait in kernel */
	while(sys_semacquire(&l->sem, 1) < 0){
		/* interrupted; try again */
	}
}

void
jehanne_unlock(Lock *l)
{
	if(jehanne_adec(&l->key) == 0)
		return;	/* changed from 1 -> 0: no contention */
	sys_semrelease(&l->sem, 1);
}

int
jehanne_canlock(Lock *l)
{
	if(jehanne_ainc(&l->key) == 1)
		return 1;	/* changed from 0 -> 1: success */
	/* Undo increment (but don't miss wakeup) */
	if(jehanne_adec(&l->key) == 0)
		return 0;	/* changed from 1 -> 0: no contention */
	sys_semrelease(&l->sem, 1);
	return 0;
}

int
jehanne_lockt(Lock *l, uint32_t ms)
{
	if(jehanne_ainc(&l->key) == 1)
		return 1;	/* changed from 0 -> 1: we hold lock */
	/* otherwise wait in kernel */
	return jehanne_tsemacquire(&l->sem, ms);
}
