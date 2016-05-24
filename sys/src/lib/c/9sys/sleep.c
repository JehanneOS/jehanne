/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <u.h>
#include <libc.h>

void
sleep(int32_t millisecs)
{
	int64_t wakeup;

	wakeup = awake(millisecs);	// give up the processor, in any case
	if(millisecs > 0)
		while(rendezvous(&wakeup, (void*)1) == (void*)~0)
			if(awakened(wakeup))
				return;
}
