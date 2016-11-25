/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2016 Giacomo Tesio <giacomo@tesio.it>
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
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

uintptr_t
grow_bss(uintptr_t addr)
{
	ProcSegment *s, *ns;
	uintptr_t newtop;
	long newsize;
	int i;

	s = up->seg[BSEG];
	if(s == nil)
		panic("grow_bss: no bss segment");

	if(addr == 0)
		return s->top;

	qlock(&s->ql);
	if(waserror()){
		qunlock(&s->ql);
		nexterror();
	}

	DBG("grow_bss addr %#p base %#p top %#p\n",
		addr, s->base, s->top);
	/* We may start with the bss overlapping the data */
	if(addr < s->base) {
		if(up->seg[DSEG] == 0 || addr < up->seg[DSEG]->base)
			error(Enovmem);
		addr = s->base;
	}

	newtop = ROUNDUP(addr, PGSZ);
	newsize = (newtop - s->table->base)/PGSZ;


	DBG("grow_bss addr %#p newtop %#p newsize %ld\n", addr, newtop, newsize);

	if(newtop < s->top) {
		/* for simplicity we only allow the bss to grow,
		 * memory will be freed on process exit
		 */
		panic("grow_bss: shrinking bss");
	}

	rlock(&up->seglock);
	for(i = 0; i < NSEG; i++) {
		ns = up->seg[i];
		if(ns == 0 || ns == s)
			continue;
		if(newtop >= ns->base && newtop < ns->top){
			runlock(&up->seglock);
			error(Esoverlap);
		}
	}
	runlock(&up->seglock);

	if(!umem_available(newtop - s->top))
		error(Enovmem);

	if(!segment_grow(s, newtop))
		error(Enovmem);

	poperror();
	qunlock(&s->ql);

	return s->top;
}

uintptr_t
sysbrk_(void* addrp)
{
	uintptr_t addr;
	addr = PTR2UINT(addrp);

	/* NOTE: this assumes that a bss segment ALWAYS exists.
	 * Thus we always add one on sysexec (see sysproc.c).
	 */
	grow_bss(addr);

	return 0;
}
