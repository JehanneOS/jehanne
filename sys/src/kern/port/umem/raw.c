/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
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
#include "../port/umem/internals.h"

/* Memory areas can be
 *
 * - SgPhysical:	for example a DMA area, a framebuffer or a
 * 			fixed segment created by devsegment
 *
 * - SgShared:		as "shared" or any segment created by devsegment
 *
 * - SgBSS:		as "memory" or another segment created
 * 			with SgBSS by segment attach
 *
 * Collectively, we call areas of type SgShared and SgBSS "virtual"
 * as they are not identified by a physical address (pa).
 * We also keep them at the end of the memory_areas array, sorted by
 * name, so that rawmem_find can use a binary search to find them.
 *
 * SgPhysical areas are instead kept at the beginning of the
 * memory_areas array, sorted by physical address, so that a binary
 * search will be the fastest way to find the matching one in rawmem_lookup
 */
static RawMemory* memory_areas;
static int areas_allocated;
static int areas_used;
static int virtual_areas;

static RWlock areas_lock;

void
rawmem_init(void)
{
	areas_allocated = 8;
	memory_areas = malloc(areas_allocated*sizeof(RawMemory));
	if(memory_areas == nil)
		panic("rawmem_init: out of memory");
	rawmem_register("shared", 0, SgShared, -1);
	rawmem_register("memory", 0, SgBSS, -1);
}

int
rawmem_register(char* name, uintptr_t pa, unsigned int attr, uintptr_t size)
{
	uintptr_t opa, osize;
	unsigned int oattr;
	int used_areas;
	RawMemory* tmp;

	if(name == nil)
		panic("rawmem_register: nil name, pc %#p", getcallerpc());
	if(attr == 0)
		panic("rawmem_register: zero attr, pc %#p", getcallerpc());
	if(size == 0)
		panic("rawmem_register: zero size, pc %#p", getcallerpc()); /* any valid use case? */

	switch(attr&SegmentTypesMask){
	case SgPhysical:
		if(pa == 0)
			panic("rawmem_register: zero pa on Physical memory area, pc %#p", getcallerpc());
		break;
	case SgShared:
	case SgBSS:
		if(pa != 0)
			panic("rawmem_register: zero pa on Virtual memory area, pc %#p", getcallerpc());
		break;
	case SgLoad:
	case SgStack:
		panic("rawmem_register: %s segment not allowed, pc %#p", segment_types[attr&SegmentTypesMask], getcallerpc());
	default:
		panic("rawmem_register: unknown segment type %uxd, pc %#p", attr&SegmentTypesMask, getcallerpc());
	}

	used_areas = areas_used;
LookupArea:
	if(rawmem_find(&name, &opa, &oattr, &osize))
		if(opa == pa && oattr == attr && osize == size)
			return 1;	/* save work */
		else
			panic("rawmem_register: '%s' already registered, pc %#p", getcallerpc());

	wlock(&areas_lock);
	if(used_areas != areas_used){
		/* if any area has been added while waiting the wlock,
		 * repeat the search
		 */
		used_areas = areas_used;
		wunlock(&areas_lock);
		goto LookupArea;
	}
	if(areas_used == areas_allocated){
		areas_allocated += 8;
		tmp = realloc(memory_areas, areas_allocated*sizeof(RawMemory));
		if(tmp == nil)
			goto RegisterFailed;
		memory_areas = tmp;
	}

	if(pa == 0){
		tmp = memory_areas+areas_used-virtual_areas;
		while(tmp < memory_areas+areas_used){
			if(strcmp(tmp->name, name) > 0)
				break;
			++tmp;
		}
		memmove(tmp+1, tmp, sizeof(RawMemory)*(memory_areas+areas_used-tmp));
		++virtual_areas;
	} else {
		tmp = memory_areas;
		while(tmp < memory_areas+areas_used-virtual_areas){
			if(tmp->pa > pa){
				if(pa + size > tmp->pa)
					panic("rawmem_register: '%s' overlaps with '%s', pc %#p", name, tmp->name, getcallerpc());
				break;
			}
			if(tmp->pa + tmp->size > pa)
				panic("rawmem_register: '%s' overlaps with '%s', pc %#p", name, tmp->name, getcallerpc());
			++tmp;
		}
		memmove(tmp+1, tmp, sizeof(RawMemory)*(memory_areas+areas_used-tmp));
	}

	tmp->name = name;	/* TODO: ensure this is a readonly constant */
	tmp->pa = pa;
	tmp->attr = attr;
	tmp->size = size;

	++areas_used;

	wunlock(&areas_lock);
	return 1;

RegisterFailed:
	wunlock(&areas_lock);
	return 0;
}

int
rawmem_find(char** name, uintptr_t *pa, unsigned int *attr, uintptr_t *size)
{
	int b, t, i, c;
	RawMemory* result;

	if(name == nil)
		panic("rawmem_find: nil name pointer, pc %#p", getcallerpc());
	if(*name == nil)
		panic("rawmem_find: nil name, pc %#p", getcallerpc());

	result = nil;

	/* try a binary search in virtual areas (memory_areas+virtual_areas)
	 * since they are sorted by name, and fallback to a for loop
	 * on physical areas (memory_areas[0 to used_areas - virtual_areas])
	 * when it find nothing.
	 */
	rlock(&areas_lock);
	b = areas_used - virtual_areas;
	t = areas_used;
	while(result == nil && b < t)
	{
		i = b + ((t - b)/2);
		if((c = strcmp(memory_areas[i].name, *name)) == 0)
			result = &memory_areas[i];
		else if(c < 0)
			b = i + 1;
		else /* c > 0 */
			t = i;
	}
	t = areas_used - virtual_areas;
	for(i = 0; result == nil && i < t; ++i)
		if(strcmp(*name, memory_areas[i].name) == 0)
			result = &memory_areas[i];
	if(result != nil){
		*name = memory_areas[i].name;
		if(pa)
			*pa = result->pa;
		if(attr)
			*attr = result->attr;
		if(size)
			*size = result->size;
	}
	runlock(&areas_lock);
	return result != nil ? 1 : 0;
}

int
rawmem_lookup(uintptr_t addr, char** name, uintptr_t *pa, unsigned int *attr, uintptr_t *size)
{
	int b, t, i;
	long c;
	RawMemory* result;

	if(addr == 0)
		panic("rawmem_lookup: zero addr, pc %#p", getcallerpc());

	b = 0;
	result = nil;

	rlock(&areas_lock);
	t = areas_used - virtual_areas;
	while(result == nil && b < t) {
		i = b + ((t - b)/2);
		if((c = memory_areas[i].pa - addr) <= 0 && addr < memory_areas[i].pa + memory_areas[i].size )
			result = &memory_areas[i];
		else if(c < 0)
			b = i + 1;
		else /* c > 0 */
			t = i;
	}
	if(result != nil){
		if(name){
			/* NOTE: we should create a copy of name, at least
			 * until we don't ensure that it is a string from a
			 * readonly page in the kernel image.
			 */
			*name = result->name;
		}
		if(pa)
			*pa = result->pa;
		if(attr)
			*attr = result->attr;
		if(size)
			*size = result->size;
	}
	runlock(&areas_lock);
	return result != nil ? 1 : 0;
}
