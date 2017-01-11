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

#define MLG(fmt, ...) //print("%#p %s[%d] %#p %s: " fmt "\n", up, up?up->text:"kernel", up?up->pid:-1, getcallerpc(), __func__, __VA_ARGS__)

typedef unsigned int PagePointer;

/* Initialize the user memory system */
extern void umem_init(void);

/* Test user memory system (while work in progress) */
extern void umem_test(void);

extern int umem_available(uintptr_t need);

/* Atomically assign a new virtual Page to *slot.
 *
 * Returns 1 on success, 0 on failure.
 *
 * Failures include:
 * - not enough memory in the system
 * - slot already filled when the page is ready.
 */
extern int page_new(PagePointer *slot, int clear);

/* Atomically set *slot to nil and dispose the page previously there.
 *
 * Returns 1 on success, 0 on failure.
 */
extern int page_dispose(PagePointer *slot);

/* Atomically assign to an empty target a page equivalent to page.
 *
 * Obviously, this function exists because you should do no assumption
 * on the value of *target after a successful call, except that the
 * memory pointed by it is the same pointed by page.
 *
 * Returns 1 if at exit *target points to the same memory of page,
 * 0 otherwise.
 */
extern int page_assign(PagePointer *target, PagePointer page);

/* Replace the page in *slot with a copy
 *
 * Returns 1 on success, 0 on failure.
 *
 * Failures can be caused by:
 * - not enough memory in the system
 * - slot empty on entry
 * - slot changed during the operation (should we panic instead?)
 */
extern int page_duplicate(PagePointer *slot);

/* just like page_duplicate but does not duplicate
 * if exists only one reference to the page
 */
extern int page_duplicate_shared(PagePointer *slot);

/* Return the address of the physical memory referenced by page */
extern uintptr_t page_pa(PagePointer page);

/* Return a pointer to the physical memory referenced by page */
extern char* page_kmap(PagePointer page);

/* Dispose the pointer to the physical memory referenced by page */
extern void page_kunmap(PagePointer page, char **memory);

/* A PageTableEntry holds actual UserPages
 * It allows to split the segment memory into chunks
 * that are going to be allocated on demand.
 */
typedef struct PageTableEntry
{
	PagePointer	pages[PTEPERTAB];	/* Page map for this chunk of pte */
	uint8_t		first;			/* First used entry */
	uint8_t		last;			/* Last used entry */
} PageTableEntry;

typedef struct PageTable
{
	uintptr_t	base;	/* base address of the fist page
				 * it might differ from the segment's base
				 */
	unsigned short	mapsize;/* => max segment size is 64 GiB */
	PageTableEntry*	map[];
} PageTable;

extern int rawmem_register(char* name, uintptr_t pa, unsigned int attr, uintptr_t size);

/* find the RawMemory *name and fill the required infos
 *
 * Return 1 on success, 0 on failure
 * On success *name is replaced with the string present in the found memory
 */
extern int rawmem_find(char** name, uintptr_t *pa, unsigned int *attr, uintptr_t *size);

extern int rawmem_lookup(uintptr_t addr, char** name, uintptr_t *pa, unsigned int *attr, uintptr_t *size);

#define	NLOAD		2	/* Number of load sections handled */

typedef unsigned int ElfSegPointer;

typedef short ImagePointer;

typedef enum SegmentType
{
	SgLoad		= 0x0,	/* in elf, replaces text and data */
	SgBSS		= 0x1,
	SgStack		= 0x2,
	SgShared	= 0x3,
	SgPhysical	= 0x4
} SegmentType;
#define SegmentTypesMask	0x7
extern char *segment_types[SegmentTypesMask+1];

typedef enum SegFlag
{
	SgCached	= 1<<0,		/* RawMemory can be cached */
	SgCExec		= 1<<1		/* Detach at exec */
} SegFlag;
#define SegFlagMask	0x3

typedef enum SegPermission
{
	SgRead		= 1<<0,
	SgWrite		= 1<<1,
	SgExecute	= 1<<2
} SegPermission;
#define SegPermissionMask	0x7

/* NOTE: we keep FaultTypes in sync with SegPermission */
typedef enum FaultType
{
	FaultRead	= 1<<0,
	FaultWrite	= 1<<1,
	FaultExecute	= 1<<2
} FaultType;
#define FaultTypeMask		0x7
extern char *fault_types[];	/* defined in segments.c */

typedef struct ProcSegment
{
	Ref		r;
	SegmentType 	type		: 3;
	SegPermission	permissions	: 3;
	SegFlag		flags		: 2;
	QLock		ql;		/* grow or shrink */
	uintptr_t	base;		/* virtual base */
	uintptr_t	top;		/* virtual top */
	PageTable*	table;		/* physical pages mapped to virtual base */

	union {
		ElfSegPointer	image;	/* prototype in file attached to this segment */
		char*		vmem;	/* name of "virtual" memory area attached (see umem/raw.c) */
		uintptr_t	pmem;	/* base address of physical memory area attached (see umem/raw.c) */
	};

	/* semacquire/semrelease */
	Lock		semalock;
	Sema		sema;
} ProcSegment;

extern int image_attach(ImagePointer* slot, Chan *c, Ldseg* segments);

extern Chan* image_chan(ElfSegPointer image);

extern int image_segments(ElfSegPointer segments[NLOAD], ImagePointer image);

extern void image_assign(ImagePointer *slot, ImagePointer img);

/* 1 if ptr refer to an attached image, 0 otherwise */
extern int image_known(ElfSegPointer ptr);

/* Fill *page (if zero) with the page from segment holding va.
 *
 * Returns 1 on success.
 *
 * If the operation requires I/O and such I/O fails, it will notify
 * and pexit the current process, *without* returning to the caller.
 *
 * Return 0 as failure if
 * - *page is already filled (might occurs if different processes sharing
 *   the same image segment race over the page)
 * - there is not enough memory available to perform the operation
 */
extern int image_fill(PagePointer* page, ElfSegPointer segment, uintptr_t va);

extern void image_release(ImagePointer ptr);

extern char* segment_name(ProcSegment *s);

extern int segment_userinit(ProcSegment** slot, int text);

extern int segment_global(ProcSegment** slot, SegFlag flags, uintptr_t va, char *name);

extern int segment_load(ProcSegment** slot, ElfSegPointer segment, Ldseg* elfinfo);

extern int segment_physical(ProcSegment** slot, SegPermission permissions, SegFlag flags, uintptr_t va, uintptr_t pa);

extern int segment_virtual(ProcSegment** slot, SegmentType type, SegPermission permissions, SegFlag flags, uintptr_t base, uintptr_t top);

extern int segment_fault(uintptr_t *mmuphys, uintptr_t *va, ProcSegment* segment, FaultType type);

extern int segment_fork(ProcSegment **s);

extern int segment_share(ProcSegment **s);

extern int segment_grow(ProcSegment *s, uintptr_t top);

/* replaces the text segment *s with a copy that has debugging enable/disabled
 * (or just inform if *s has debugging enabled)
 * - if enable is nil, returns 1 if debug is enabled in *s, 0 otherwise
 * - if *enable is 0, try to replace *s with a NOT debuggable copy,
 *   returning 0 if it's *s is already not debuggable and 1 otherwise
 * - otherwise, try to replace *s with a debuggable copy, returning 0
 *   if it's already a debuggable segment and 1 otherwise
 */
extern int segment_debug(ProcSegment** s, int* enable);

/* return the PagePointer for va, or zero if the page has not been faulted yet */
extern PagePointer segment_page(ProcSegment* s, uintptr_t va);

extern void segment_free_pages(ProcSegment *s, uintptr_t from, uintptr_t to);

extern void segment_release(ProcSegment** s);

/* these should be in a proc related header */
extern ProcSegment* proc_segment(Proc *p, uintptr_t va);
extern void proc_check_pages(void);
extern int proc_own_pagepool(Proc *p);
extern int proc_segment_detach(Proc *p, uintptr_t va);

typedef struct MemoryStats
{
	unsigned long memory;
	unsigned long kernel;
	unsigned long user_available;
	unsigned long user;
} MemoryStats;

/* fills stats with memory statistics */
extern void memory_stats(MemoryStats *stats);
