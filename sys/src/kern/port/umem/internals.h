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

/*
 * This file declares functions and types that are internal to the module.
 */

typedef struct UserPage
{
	Ref		r;		/* Reference count */
	int		next;		/* as index in free list */
	uintptr_t	pa;		/* Physical address in memory */
} UserPage;

typedef struct ElfSegment
{
	QLock		l;		/* lock for I/O */
	unsigned int	npages;		/* size in pages */
	uint32_t	fstart;		/* start address in file for demand load */
	uint32_t	fend;		/* end of segment in file */
	uintptr_t	pg0addr;	/* address of the first virtual page */
	uint32_t	pg0off;		/* offset of the first byte in the first page */
	PagePointer*	pages;		/* Pages assigned, once loaded; entry is nil if still on file */
} ElfSegment;

typedef struct ElfImage
{
	Lock		l;
	Chan*		c;		/* channel to text file */
	Qid 		qid;		/* Qid for page cache coherence */
	uint64_t	mpath;
	Chan*		mchan;
	int		dc;		/* Device type of owning channel */
	Ref		r;
	int		pcount;		/* number of pages loaded */
	unsigned short	fnext;		/* next in the free list */
	unsigned short	hnext;		/* next in the hash chain */
	ElfSegment	seg[NLOAD];
} ElfImage;

typedef struct RawMemory
{
	unsigned int	attr;	/* Segment attributes */
	char*		name;	/* Attach name */
	uintptr_t	pa;	/* Physical address */
	uintptr_t	size;	/* Maximum segment size in bytes */
} RawMemory;

/* Initialize an empty table to map from base to top */
extern int table_new(PageTable** table, uintptr_t base, uintptr_t top);

/* Initialize target table as a copy of the original */
extern int table_clone(PageTable* target, PageTable* original);

/* fully free the page table pointed by target */
extern void table_free(PageTable** target);

extern int table_resize(PageTable** target, uintptr_t top);

/* dispose the pages in table, but preserve the table itself
 * (and PageTableEntries)
 */
extern void table_free_pages(PageTable* table);

/* Lookup the page containing the virtual address va
 *
 * NOTE: it could sleep when an allocation is required, so it must be
 *       called out of any lock at least once.
 */
extern PagePointer* table_lookup(PageTable* table, uintptr_t va);

extern void table_walk_pages(PageTable* table, void (*func)(uintptr_t, UserPage*));

extern void elfsegment_assign(ElfSegPointer *target, ElfSegPointer img);

extern void rawmem_init(void);

extern void imagepool_init(void);
