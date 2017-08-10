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

#include "init.h"

char *fault_types[] = {
	[FaultWrite] "write",
	[FaultRead] "read",
	[FaultExecute] "exec"
};

char *segment_types[SegmentTypesMask+1]={
	[SgLoad] "Load",
	[SgBSS] "Bss",
	[SgStack] "Stack",
	[SgShared] "Shared",
	[SgPhysical] "Physical",
};

char *
segment_name(ProcSegment *s)
{
	if(s == nil)
		panic("segment_name: nil segment, pc %#p", getcallerpc());
	switch(s->type){
	case SgLoad:
		if(s->image % NLOAD)
			return "Text";
		else
			return "Data";
	case SgBSS:
	case SgStack:
	case SgShared:
	case SgPhysical:
		return segment_types[s->type];
	default:
		panic("segment_name: unknown segment type %d, pc %#p", s->type, getcallerpc());
	}
}

PagePointer
segment_page(ProcSegment* s, uintptr_t va)
{
	PagePointer* page;
	if(s == nil)
		panic("proc_segment: nil process, pc %#p", getcallerpc());
	page = table_lookup(s->table, va);
	return *page;
}


static ProcSegment*
segment_alloc(SegmentType type, SegPermission permissions, SegFlag flags, uintptr_t base, uintptr_t top)
{
	ProcSegment* s;
	s = jehanne_mallocz(sizeof(ProcSegment), 1);
	if(s == nil)
		return nil;
	s->r.ref = 1;
	s->type = type;
	s->permissions = permissions;
	s->flags = flags;
	s->base = base;
	s->top = top;
	if(type != SgPhysical	/* Physical segments do not need pages */
	&& !table_new(&s->table, base, top)){
		jehanne_free(s);
		return nil;
	}

	s->sema.prev = &s->sema;
	s->sema.next = &s->sema;

	return s;
}

/* this constructor is for exclusive use of main.c */
int
segment_userinit(ProcSegment** slot, int data)
{
	static int text_initialized;
	static int data_initialized;
	static int called;

	ProcSegment* new;
	PagePointer *page;
	SegPermission permissions;
	uintptr_t base, top;
	char *content, *tmp;
	int i, shared;

	if(called > 2 || (text_initialized && data_initialized))
		panic("segment_userinit: third call, pc %#p", getcallerpc());
	if(slot == nil)
		panic("segment_userinit: nil slot, pc %#p", getcallerpc());
	if(*slot != nil)
		panic("segment_userinit: dirty slot, pc %#p", getcallerpc());
	++called;
	if(data){
		++data_initialized;
		permissions = SgRead|SgWrite;
		base = init_data_start;
		top = init_data_end;
		content = (char*)init_data_out;
	} else {
		++text_initialized;
		permissions = SgRead|SgExecute;
		base = init_code_start;
		top = init_code_end;
		content = (char*)init_code_out;
	}
	shared = ROUNDUP(top - base, PGSZ)/PGSZ;
	new = segment_alloc(
		SgLoad,
		permissions,
		0,
		base,
		top
	);
	if(new == nil)
		return 0;

	for(i = 0; i < shared; ++i){
		page = table_lookup(new->table, base+(i*PGSZ));
		if(!page_new(page, i == (shared-1)))
			panic("segment_userinit: out of memory, pc %#p", getcallerpc());
		tmp = page_kmap(*page);
		jehanne_memmove(tmp, content+i*PGSZ, MIN(PGSZ, top-base-i*PGSZ));
		page_kunmap(*page, &tmp);
	}
	new->image = data;
	if(!CASV(slot, nil, new))
		panic("segment_userinit: slot got dirty, pc %#p", getcallerpc());
	return 1;
}

int
segment_load(ProcSegment** slot, ElfSegPointer segment, Ldseg* elfinfo)
{
	ProcSegment* new;
	SegPermission permissions;
	uintptr_t top;

	if(slot == nil)
		panic("segment_load: nil slot, pc %#p", getcallerpc());
	if(*slot != nil)
		panic("segment_load: dirty slot, pc %#p", getcallerpc());
	if(elfinfo == nil)
		panic("segment_load: nil elfinfo, pc %#p", getcallerpc());

	switch(segment % NLOAD){
	case 0:
		/* text segment */
		permissions = SgRead|SgExecute;
		top = elfinfo->pg0vaddr + elfinfo->pg0off + elfinfo->memsz;
		break;
	case 1:
		/* data segment
		 * we use filesz instead of memsz to keep bss in a
		 * different segment...
		 */
		permissions = SgRead|SgWrite;
		top = elfinfo->pg0vaddr + elfinfo->pg0off + elfinfo->filesz;
		/* ...but rounded to PGSZ */
		top = ROUNDUP(top, PGSZ);
		break;
	default:
		panic("segment_load: unknown segment subtype %d, pc %#p", segment%NLOAD, getcallerpc());
	}
	new = segment_alloc(
		SgLoad,
		permissions,
		0,
		elfinfo->pg0vaddr + elfinfo->pg0off,
		top
	);
	if(new == nil)
		return 0;
	elfsegment_assign(&new->image, segment);

	if(!CASV(slot, nil, new))
		panic("segment_load: slot got dirty, pc %#p", getcallerpc());
	return 1;
}

int
segment_physical(ProcSegment** slot, SegPermission permissions, SegFlag flags, uintptr_t va, uintptr_t pa)
{
	ProcSegment* new;
	uintptr_t mpa;
	unsigned int mattr;
	uintptr_t size;
	if(slot == nil)
		panic("segment_physical: nil slot, pc %#p", getcallerpc());
	if(*slot != nil)
		panic("segment_physical: dirty slot, pc %#p", getcallerpc());
	if(pa == 0)
		panic("segment_physical: zero pa, pc %#p", getcallerpc());
	if(!rawmem_lookup(pa, nil, &mpa, &mattr, &size))
		panic("segment_physical: no physical memory area at %#p, pc %#p", pa, getcallerpc());
	if(va == 0)
		panic("segment_physical: zero va, pc %#p", getcallerpc());
	va = va & ~(PGSZ-1);
	new = segment_alloc(
		SgPhysical,
		permissions&SegPermissionMask&(~SgExecute),
		flags&SegFlagMask,
		va,
		ROUNDUP(va + size, PGSZ)
	);
	if(new == nil)
		return 0;
	new->pmem = mpa;

	if(!CASV(slot, nil, new))
		panic("segment_load: slot got dirty, pc %#p", getcallerpc());
	return 1;
}

int
segment_global(ProcSegment** slot, SegFlag flags, uintptr_t va, char *name)
{
	ProcSegment* new;
	unsigned int mattr;
	uintptr_t size;
	SegmentType type;
	if(slot == nil)
		panic("segment_global: nil slot, pc %#p", getcallerpc());
	if(*slot == nil)
		panic("segment_global: dirty slot, pc %#p", getcallerpc());
	if(name == nil)
		panic("segment_global: nil name, pc %#p", getcallerpc());
	if(!rawmem_find(&name, nil, &mattr, &size))
		return 0;
	type = mattr&SegmentTypesMask;
	if(type != SgShared && type != SgBSS)
		if(type > SgPhysical)
			panic("segment_global: unknown segment type %d for memory area '%s', pc %#p", type, name, getcallerpc());
		else
			panic("segment_global: type %s for memory area '%s' not allowed, pc %#p", segment_types[type], name, getcallerpc());
	new = segment_alloc(
		type,
		mattr&SegPermissionMask&(~SgExecute),
		flags&mattr&SegFlagMask,
		va,
		va + size*PGSZ
	);
	if(new == nil)
		return 0;
	new->vmem = name;

	if(!CASV(slot, nil, new))
		panic("segment_load: slot got dirty, pc %#p", getcallerpc());
	return 1;
}

int
segment_virtual(ProcSegment** slot, SegmentType type, SegPermission permissions, SegFlag flags, uintptr_t base, uintptr_t top)
{
	ProcSegment* new;

	if(slot == nil)
		panic("segment_virtual: nil slot, pc %#p", getcallerpc());
	if(*slot != nil)
		panic("segment_virtual: dirty slot, pc %#p", getcallerpc());
	if(type == SgLoad || type == SgPhysical)
		panic("segment_virtual: type %s, pc %#p", segment_types[type], getcallerpc());
	if(type > SgPhysical)
		panic("segment_virtual: unknown type %d, pc %#p", type, getcallerpc());
	if(permissions & SgExecute)
		panic("segment_virtual: executable segment, pc %#p", segment_types[type], getcallerpc());
	if(top < base)
		panic("segment_virtual: top < base, pc %#p", getcallerpc());
	new = segment_alloc(
		type,
		permissions&SegPermissionMask&(~SgExecute),
		flags&SegFlagMask,
		base,
		top
	);
	if(new == nil)
		return 0;

	if(!CASV(slot, nil, new))
		panic("segment_load: slot got dirty, pc %#p", getcallerpc());
	return 1;
}

static int
segment_fault_physical(uintptr_t *mmuphys, uintptr_t *va, ProcSegment* segment, FaultType type)
{
	uintptr_t pa, mmuflags;
	switch(type){
	default:
		panic("segment_fault_text: unknown fault type %d", type);
	case FaultExecute:
		return 0;
	case FaultRead:
	case FaultWrite:
		if(!rawmem_lookup(segment->pmem, nil, &pa, nil, nil))
			return 0;
		pa += (*va - segment->base);
		mmuflags = PTEVALID;
		if((segment->permissions & SgWrite))
			mmuflags |= PTEWRITE;
		else
			mmuflags |= PTERONLY;
		if((segment->flags & SgCached) == 0)
			mmuflags |= PTEUNCACHED;
		*mmuphys = PPN(pa) | mmuflags;
		MLG("segment %#p, fault %s *page %ud mmuflags %#p *mmuphys %#p", segment, fault_types[type], mmuflags, *mmuphys);
		return 1;
	}
}

int
segment_fault(uintptr_t *mmuphys, uintptr_t *va, ProcSegment* segment, FaultType type)
{
	PagePointer *page;
	SegmentType stype;
	if(nil == mmuphys)
		panic("segment_fault: nil mmuphys, pc %#p", getcallerpc());
	if(nil == va)
		panic("segment_fault: nil va, pc %#p", getcallerpc());
	if(nil == segment)
		panic("segment_fault: nil segment, pc %#p", getcallerpc());
	if((segment->permissions&((SegPermission)type)) == 0)
		return 0;
	MLG("mmuphys %#p, *va %#p segment %#p fault %s", mmuphys, *va, segment, fault_types[type]);
	*va = *va&~(PGSZ-1);

	stype = segment->type;

	if(stype == SgPhysical)
		return segment_fault_physical(mmuphys, va, segment, type);

	page = table_lookup(segment->table, *va);
	if(page == nil)
		return 0;
	switch(stype){
	default:
		panic("segment_fault: unknown segment type %d, pc %#p", type, getcallerpc());

	case SgLoad:
		if(*page == 0){
			if(!image_fill(page, segment->image, *va)){
				if(*page == 0)
					return 0; /* out of memory */
				if(segment->image%NLOAD == 0		/* text segment */
				&&(segment->permissions&SgWrite)){	/* debug mode */
					/* each writable text segment belongs to
					 * a single process (see segment_debug)
					 * thus it's not possible that a fault
					 * from another process sharing this
					 * segment could have turn *page in a
					 * writable copy already
					 */
					panic("segment_fault: race on image_fill (text va %#p)", *va);
				}
			}
		}
		if(segment->image % NLOAD)
			goto DataSegment;
		if(segment->permissions&SgWrite){
			/* debuggable text segment */
			if(!page_duplicate(page))
				return 0;
			*mmuphys = PPN(page_pa(*page)) | PTEWRITE|PTEVALID;
		} else
			*mmuphys = PPN(page_pa(*page)) | PTERONLY|PTEVALID;
		break;

	case SgBSS:
	case SgShared:
	case SgStack:
		if(*page == 0){
			if(!page_new(page, 1) && *page == 0)
				return 0;
		}
DataSegment:
		qlock(&segment->ql);
		if(type == FaultRead && sys->copymode == 0 && segment->r.ref == 1) {
			*mmuphys = PPN(page_pa(*page))|PTERONLY|PTEVALID;
		} else {
			if(!page_duplicate_shared(page)){
				qunlock(&segment->ql);
				return 0;
			}
			*mmuphys = PPN(page_pa(*page))|PTEWRITE|PTEVALID;
		}
		qunlock(&segment->ql);
		break;
	}
	return 1;
}

static void
segment_info(ProcSegment* segment, Ldseg *info)
{
	info->memsz = segment->top - segment->base;
	info->filesz = segment->top - segment->base;
	info->pg0vaddr = segment->base & ~(PGSZ-1);
	info->pg0off = segment->base & (PGSZ-1);
	info->pgsz = PGSZ;
}

void
segment_release(ProcSegment** s)
{
	ProcSegment *segment;
	if(nil == s)
		panic("segment_release: nil segment pointer, pc %#p", getcallerpc());
	segment = xchgm(s, nil);
	if(nil == segment)
		panic("segment_release: nil segment, pc %#p", getcallerpc());
	MLG("segment %#p segment->type %d, ref %d", segment, segment->type, segment->r.ref);
	if(decref(&segment->r) > 0)
		return;
	switch(segment->type){
		default:
			panic("segment_release: unknown segment type %d, pc %#p", segment->type, getcallerpc());
		case SgLoad:
			if(segment->image > 1
			||(up->parentpid == 1 && up->text[0] != '*'))
				image_release(segment->image/NLOAD);
			/* wet floor */
		case SgBSS:
		case SgShared:
		case SgStack:
			table_free(&segment->table);
			break;
		case SgPhysical:
			break;
	}
	jehanne_free(segment);
}

/* replaces the segment in s with a copy with debug enabled/disabled
 *
 * a debuggable text segment is just a text segment with SgWrite permission
 * see
 * 	http://eli.thegreenplace.net/2011/01/27/how-debuggers-work-part-2-breakpoints
 * 	http://www.alexonlinux.com/how-debugger-works
 *
 * segment_debug take a ProcSegment** because, by replacing the pointer
 * in the process' segment table it ensure that only the process
 * being debugged will see the breakpoints.
 */
/* assumes proc->seglock wlocked */
int
segment_debug(ProcSegment** s, int* enable)
{
	ProcSegment *segment, *new;
	SegPermission p;
	Ldseg info;

	if(nil == s)
		panic("segment_debug: nil segment pointer, pc %#p", getcallerpc());
	segment = *s;
	if(nil == segment)
		panic("segment_debug: nil segment, pc %#p", getcallerpc());

	if(segment->type != SgLoad || segment->image % NLOAD != 0)
		panic("segment_debug: not a text segment, pc %#p", getcallerpc());
	p = segment->permissions;
	if(enable == nil)
		return (p&SgWrite) ? 1 : 0;
	if(*enable && (p&SgWrite))
		return 0;	/* already enabled */
	if(*enable == 0 && (p&SgWrite) == 0)
		return 0;	/* already disabled */
	new = nil;
	segment_info(segment, &info);
	if(!segment_load(&new, segment->image, &info))
		return 0;
	if(*enable)
		new->permissions &= SgWrite;
	*s = new;
	segment_release(&segment);
	return 1;
}

/* assumes proc->seglock wlocked */
int
segment_share(ProcSegment **s)
{
	ProcSegment *segment, *new;
	Ldseg info;

	if(nil == s)
		panic("segment_share: nil segment pointer, pc %#p", getcallerpc());
	segment = *s;
	if(nil == segment)
		panic("segment_share: nil segment, pc %#p", getcallerpc());
	new = nil;

	switch(segment->type){
	default:
		panic("segment_share: unknown segment type %d, pc %#p", segment->type, getcallerpc());
	case SgLoad:
		if(segment->image % NLOAD == 0
		&& segment->permissions&SgWrite){
			/* text segment in debug mode: on rfork(whatever)
			 * it is going to be copied as a readonly
			 */
			segment_info(segment, &info);
			if(!segment_load(&new, segment->image, &info))
				return 0;
			*s = new;
			MLG("segment %#p Text in DEBUG ref %d FORKED", segment, segment->r.ref);
			return 1;
		}
		break;
	case SgStack:
		/* stack is always private */
		if(!segment_virtual(&new, SgStack, segment->permissions, segment->flags, segment->base, segment->top))
			return 0;
		qlock(&segment->ql);
		if(!table_clone(new->table, segment->table)){
			qunlock(&segment->ql);
			segment_release(&new);
			return 0;
		}
		qunlock(&segment->ql);
		if(segment->r.ref > 1)
			procs_flush_segment(segment);
		*s = new;
		MLG("segment %#p segment->type %s ref %d FORKED", segment, segment_name(segment), segment->r.ref);
		return 1;
	case SgShared:
	case SgPhysical:
	case SgBSS:
		break;
	}
	incref(&segment->r);
	MLG("segment %#p segment->type %d ref %d ACTUALLY SHARED", segment, segment->type, segment->r.ref);
	return 1;
}

/* assumes proc->seglock wlocked */
int
segment_fork(ProcSegment **s)
{
	ProcSegment *segment, *new;
	Ldseg info;
	if(nil == s)
		panic("segment_fork: nil segment pointer, pc %#p", getcallerpc());
	segment = *s;
	if(nil == segment)
		panic("segment_fork: nil segment, pc %#p", getcallerpc());
	new = nil;
	switch(segment->type){
	case SgLoad:
		if(segment->image % NLOAD != 0){
			/* data segment */
			segment_info(segment, &info);
			if(!segment_load(&new, segment->image, &info))
				return 0;
			goto CloneTable;
		}
		if(segment->permissions & SgWrite){
			/* text in debug mode, on rfork a new
			 * readonly segment is created
			 */
			segment_info(segment, &info);
			if(!segment_load(&new, segment->image, &info))
				return 0;
			*s = new;
			MLG("segment %#p Text in DEBUG ref %d FORKED", segment, segment->r.ref);
			return 1;
		}
		/* readonly text segments are shared */
		break;
	case SgBSS:
	case SgStack:
		if(!segment_virtual(&new, segment->type, segment->permissions, segment->flags, segment->base, segment->top))
			return 0;
CloneTable:
		qlock(&segment->ql);
		if(!table_clone(new->table, segment->table)){
			qunlock(&segment->ql);
			segment_release(&new);
			return 0;
		}
		qunlock(&segment->ql);
		if(segment->r.ref > 1)
			procs_flush_segment(segment);
		*s = new;
		MLG("segment %#p %s ref %d ACTUALLY FORKED", segment, segment_name(segment), segment->r.ref);
		return 1;
	case SgShared:
	case SgPhysical:
		break;
	}
	MLG("segment %#p %s ref %d SHARED", segment, segment_name(segment), segment->r.ref);
	incref(&segment->r);
	return 1;
}

/* assumes s->ql locked */
int
segment_grow(ProcSegment *s, uintptr_t top)
{
	int res = 0;
	if(nil == s)
		panic("segment_grow: nil segment, pc %#p", getcallerpc());
	if(top == s->top)
		return 1;
	if(top > s->top)
		res = table_resize(&s->table, top);
	if(res)
		s->top = ROUNDUP(top, PGSZ);
	return res;
}

void
segment_free_pages(ProcSegment *s, uintptr_t from, uintptr_t to)
{
	uintptr_t va;
	PagePointer *page;
	if(s == 0)
		panic("segment_free_pages: nil segment, pc %#p", getcallerpc());
	if(from >= to)
		panic("segment_free_pages: empty page set, pc %#p", getcallerpc());
	if(s->type == SgPhysical)
		return;
	qlock(&s->ql);
	if(s->r.ref > 1)
		procs_flush_segment(s);
	for(va = from; va < to; va += PGSZ){
		page = table_lookup(s->table, va);
		if(*page != 0)
			page_dispose(page);
	}
	qunlock(&s->ql);
}

void
segment_relocate(ProcSegment *s, uintptr_t newbase, uintptr_t newtop)
{
	if(s == 0)
		panic("segment_relocate: nil segment, pc %#p", getcallerpc());
	if(s->top - s->base != newtop - newbase)
		panic("segment_relocate: change in size %ulld (old) != %ulld (new), pc %#p", s->top - s->base, newtop - newbase, getcallerpc());
	if(s->r.ref > 1)
		panic("segment_relocate: relocating shared segment, pc %#p", getcallerpc());
	qlock(&s->ql);
	s->base = newbase;
	s->top = newtop;
	s->table->base = newbase;
	qunlock(&s->ql);
}

ProcSegment*
proc_segment(Proc *p, uintptr_t va)
{
	int i;
	ProcSegment *s = nil;
	if(p == nil)
		panic("proc_segment: nil process, pc %#p", getcallerpc());
	rlock(&p->seglock);
	for(i = 0; i < NSEG; ++i){
		s = p->seg[i];
		if(s && s->base <= va && va < s->top)
			break;
	}
	runlock(&p->seglock);
	if(i == NSEG)
		return nil;
	return s;
}

int
proc_segment_detach(Proc *p, uintptr_t va)
{
	int i;
	ProcSegment *s = nil;
	if(p == nil)
		panic("proc_segment_detach: nil process, pc %#p", getcallerpc());
	wlock(&p->seglock);
	for(i = 0; i < NSEG; ++i){
		s = p->seg[i];
		if(s && s->base <= va && va < s->top){
			segment_release(&p->seg[i]);
			break;
		}
	}
	wunlock(&p->seglock);
	if(i == NSEG)
		return 0;
	return 1;
}


static void
check_single_page(uintptr_t va, UserPage* page)
{
	if(!iskaddr(page))
		jehanne_print("%d %s: invalid page off %#p pg %#p\n", up->pid, up->text, va, page);
	else
		checkmmu(va, page->pa);
}

void
proc_check_pages(void)
{
	ProcSegment **sp, **ep, *s;

	if(up == nil || up->newtlb)
		return;

	rlock(&up->seglock);
	for(sp=up->seg, ep=&up->seg[NSEG]; sp<ep; sp++){
		s = *sp;
		if(s == nil || s->table == nil)
			continue;
		table_walk_pages(s->table, check_single_page);
	}
	runlock(&up->seglock);
}
