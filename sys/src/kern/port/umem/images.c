/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016-2017 Giacomo Tesio <giacomo@tesio.it>
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
#include "../port/umem/internals.h"

/* ElfImages are preallocated in chunks.
 *
 * We assume a constant number of load segments (NLOAD).
 *
 * We hash the path of each new image and add it to a chain headed in
 * pool.hash elements, so that we can reuse already loaded pages.
 *
 * When an image is not referenced we add it to the free list, but
 * we do not release the loaded pages until all the blank images in
 * the pool have been used and a new one is waiting to be added.
 *
 * When we need to reuse a free image we look for one with the least
 * pages loaded, hoping to
 * - minimize the wait for the incoming images
 * - maximize reuse of heavy images (Jehanne is distributed, thus
 *   we have to assume that loading each page is expensive)
 * - prepare for a (future) garbage collector that will free heavy
 *   unused images when the system is under memory pressure
 *
 * When a sleeping page is freed, its page tables are resized (see
 * table_resize): each of its page is disposed, but the
 * PageTableEntry is not freed unless the new image requires less
 * PageTableEntries than the sleeping one.
 *
 * A wlock protects the pool growth and the modifications
 * to the free list that affect the fnext field in one or more images.
 * All other concurrent changes to the pool run concurrently under an
 * rlock, with possible races handled by ainc/adec and cas32.
 * In practice this means that:
 * - grow_pool, freelist_add and freelist_pop will wlock/wunlock
 * - everybody else will rlock/runlock (and sometimes lock single images)
 *   - blank images are allocated with a single adec(pool.blanks)
 *   - the hash chain is updated with compare and swap
 *   - the count of sleeping images (aka freelist members) is handled by ainc/adec
 *   - to know if an image is free we check if img->r.ref == 0
 *   - to search for an image matching a chan to attach we lock(&img->l)
 *     so that it cannot change its qid (while freelist_pop modify the
 *     image structures under the wlock, we still need to lock the image
 *     so that image_attach do not need to wlock while filling a free
 *     image)
 */

#define	PSTEP		__UINT8_MAX__+1	/* must be a power of 2 */
#define MAXIMAGES	__SHRT_MAX__

typedef struct ImgPoolChunk
{
	ElfImage	images[PSTEP];
} ImgPoolChunk;

typedef struct ImagePool
{
	ImgPoolChunk**	chunks;		/* array of allocated/PSTEP chunk pointers */
	short		allocated;	/* total number of images */
	unsigned short	hash[PSTEP];	/* hash table of allocated images */
	int		sleeping;	/* number of images that can be reclaimed */
	unsigned short	first_sleeper;	/* index of first sleeping image */
	int		blanks;		/* number of blank images */
	int		max_images;	/* max number of images */
} ImagePool;

static RWlock pool_lock;	/* to grow chunks or modify free list */
static ImagePool pool;

#define img_hash(path)	((uint8_t)((path*(PSTEP+1))&(PSTEP-1)))
#define img_get(ptr)	(&(pool.chunks[(uint8_t)(ptr-1)>>8]->images[(uint8_t)(ptr-1)&(PSTEP-1)]))

void
imagepool_init(short max_images)
{
	pool.chunks = jehanne_malloc(sizeof(ImgPoolChunk*));
	if(pool.chunks == nil)
		panic("image_init: out of memory");
	pool.chunks[0] = jehanne_mallocz(sizeof(ImgPoolChunk), 1);
	if(pool.chunks[0] == nil)
		panic("image_init: out of memory");
	pool.allocated = PSTEP;
	pool.sleeping = 0;
	pool.blanks = PSTEP;
	pool.first_sleeper = PSTEP;
	if(max_images > PSTEP)
		max_images = ROUNDUP(max_images, PSTEP);
	if(max_images <= 0)
		max_images = MAXIMAGES;
	pool.max_images = max_images;
}

/* will wlock/wunlock pool_lock */
static int
imagepool_grow(void)
{
	ImgPoolChunk** chunks;
	ElfImage* fimg;
	unsigned short *next;
	int size, initially_allocated;

	wlock(&pool_lock);
	while(pool.blanks <= 0){
		if(pool.allocated + PSTEP > pool.max_images)
			goto Fail;
		initially_allocated = pool.allocated;

		size = pool.allocated/PSTEP;
		chunks = jehanne_malloc(sizeof(ImgPoolChunk*)*(size+1));
		if(chunks == nil)
			goto Fail;
		chunks[size] = jehanne_mallocz(sizeof(ImgPoolChunk), 1);
		if(chunks[size] == nil){
			goto FreeChunksAndFail;
		}
		chunks = jehanne_memmove(chunks, pool.chunks, size);

		jehanne_free(pool.chunks);
		pool.chunks = chunks;
		pool.allocated += PSTEP;
		pool.blanks += PSTEP;
		/* we have to update the last fnext in the free list:
		 * find it and cleanup the list in the process
		 */
		if(initially_allocated < pool.allocated){
			for(next = &pool.first_sleeper; *next != initially_allocated; next = &fimg->fnext){
				fimg = img_get(*next);
				if(fimg->r.ref > 0){
					/* this image is not sleeping anymore, so let
					 * update the free list while we hold the wlock
					 */
					*next = fimg->fnext;
				}
			}
			if(*next != initially_allocated)
				panic("grow_pool: cannot find the end of the freelist");
			*next = pool.allocated;
		}
	}
	wunlock(&pool_lock);

	return 1;

FreeChunksAndFail:
	jehanne_free(chunks);
Fail:
	/* reset blanks requests: the requesters will fail anyway */
	if(pool.blanks < 0)
		pool.blanks = 0;
	wunlock(&pool_lock);
	return 0;
}

static void
image_initialize(ElfImage* new, Chan *c)
{
	new->r.ref = 1;
	incref(&c->r);
	new->c = c;
	new->dc = c->dev->dc;
	new->qid = c->qid;
	new->mpath = c->mqid.path;
	new->mchan = c->mchan;
	new->hnext = 0;
}

static void
dispose_segment_pages(PagePointer* pages, unsigned int npages)
{
	if(pages == nil)
		return;
	while(npages){
		page_dispose(&pages[--npages]);
	}
}

static int
segments_fill(ElfSegment* segments, Ldseg *infos)
{
	Ldseg *text, *data;

	/* we assume exactly NLOAD (aka 2) segments, for text and data */
	text = &infos[0];
	data = &infos[1];
	if(text->pgsz != PGSZ || data->pgsz != PGSZ)
		return 0;

	dispose_segment_pages(segments[0].pages, segments[0].npages);
	dispose_segment_pages(segments[1].pages, segments[1].npages);

	segments[0].npages = HOWMANY(text->pg0off+text->memsz, PGSZ);
	if(segments[0].npages > (SEGMAPSIZE*PTEPERTAB))
		return 0;
	if(segments[0].pages != nil)
		jehanne_free(segments[0].pages);
	segments[0].pages = jehanne_mallocz(sizeof(PagePointer)*segments[0].npages, 1);
	if(segments[0].pages == nil)
		return 0;
	segments[0].pg0addr = text->pg0vaddr;
	segments[0].pg0off = text->pg0off;
	segments[0].fstart = text->pg0fileoff + text->pg0off;
	segments[0].fend = text->pg0fileoff + text->pg0off + text->filesz;

	segments[1].npages = HOWMANY(data->pg0off+data->filesz, PGSZ);
	if(segments[1].npages > (SEGMAPSIZE*PTEPERTAB))
		return 0;
	if(segments[1].pages != nil)
		jehanne_free(segments[1].pages);
	segments[1].pages = jehanne_mallocz(sizeof(PagePointer)*segments[1].npages, 1);
	if(segments[1].pages == nil){
		jehanne_free(segments[0].pages);
		segments[0].pages = nil;
		return 0;
	}
	segments[1].pg0addr = data->pg0vaddr;
	segments[1].pg0off = data->pg0off;
	segments[1].fstart = data->pg0fileoff + data->pg0off;
	segments[1].fend = data->pg0fileoff + data->pg0off + data->filesz;

	return 1;
}

/* cleanup the freelist.
 * assumes pool_lock wlocked
 */
static void
freelist_clean(void)
{
	ElfImage *img;
	unsigned short *next;

	/* note that we do not update pool.sleeping since it's already
	 * updated when a free image got reused on image_attach
	 */
	for(next = &pool.first_sleeper; *next < pool.allocated; next = &img->fnext){
		img = img_get(*next);
		if(img->r.ref > 0){
			/* this image is not sleeping anymore, so let
			 * update the free list while we hold the wlock
			 */
			*next = img->fnext;
		}
	}
}

/* will wlock/wunlock pool_lock */
static void
freelist_add(ImagePointer ptr, ElfImage *img)
{
	ElfImage *fimg;
	unsigned short *next;

	if(img_get(ptr) != img)
		panic("freelist_add: img_get(%hud) != img (%#p), pc %#p", ptr, img, getcallerpc());

	wlock(&pool_lock);
	if(img->r.ref > 0)
		return;
	freelist_clean();

	for(next = &pool.first_sleeper; *next < pool.allocated; next = &fimg->fnext){
		if(*next == ptr) /* avoid loops */
			goto AddedToFreeList;
		fimg = img_get(*next);
	}
	img->fnext = pool.first_sleeper;
	pool.first_sleeper = ptr;
AddedToFreeList:
	ainc(&pool.sleeping);
	wunlock(&pool_lock);
}

/* will wlock/wunlock pool_lock */
/* on success will add blank_to_free to the freelist */
static ImagePointer
freelist_pop(int blankslot, int originally_allocated)
{
	ElfImage *fimg, *tmp;
	unsigned short *next, *fptr;
	ImagePointer ptr = 0;

	fimg = nil;

	wlock(&pool_lock);

	/* if more blanks have been allocated after blankslot,
	 * there is no need to use the free list, use the blank slot
	 * instead
	 */
	if(originally_allocated < pool.allocated){
		ptr = pool.allocated - blankslot;
		goto Done;
	}

	if(pool.sleeping == 0)
		goto Done;
	freelist_clean();

	for(next = &pool.first_sleeper; *next < pool.allocated; next = &tmp->fnext){
		tmp = img_get(*next);
		if(fimg == nil || tmp->pcount < fimg->pcount){
			fimg = tmp;
			fptr = next;
		}
	}

	if(fimg == nil)
		panic("no free image in sleeping free list");

	mkqid(&fimg->qid, ~0, ~0, QTDIR); /* so that it cannot be found by hash */
	ptr = *fptr;
	*fptr = fimg->fnext;

	if(!cas16(&pool.blanks, blankslot, blankslot-1)){
		/* this blank slot have to be added to the freelist later,
		 * after the poll have grown enough: we will need
		 * a garbage collector for this (TO BE IMPLEMENTED)
		 */
		panic("lost image slot");
	}
Done:
	wunlock(&pool_lock);
	return ptr;
}

Chan*
image_chan(ElfSegPointer image)
{
	ElfImage *img;
	Chan *c = nil;

	img = img_get(image/NLOAD);
	lock(&img->l);
	if(img->r.ref == 0)
		goto Done;
	c = img->c;
	if(c == nil)
		goto Done;
	if(incref(&c->r) == 1 || (c->flag&COPEN) == 0 || c->mode!=OREAD){
		cclose(c);
		c = nil;
		goto Done;
	}
Done:
	unlock(&img->l);
	return c;
}

static void
image_unfill(ElfImage *img)
{
	mkqid(&img->qid, ~0, ~0, QTDIR);
	if(decref(&img->c->r) == 0)
		cclose(img->c);
	img->c = nil;
	decref(&img->r);
}

int
image_segments(ElfSegPointer segments[NLOAD], ImagePointer image)
{
	ElfImage *img;
	int i;

	img = img_get(image);
	if(img->r.ref == 0)
		return 0;

	lock(&img->l);
	for(i = 0; i < NLOAD; ++i)
		segments[i] = image*NLOAD + i;
	unlock(&img->l);
	return 1;
}

int
image_attach(ImagePointer* slot, Chan *c, Ldseg* segments)
{
	unsigned short *chain, blankslot, newp, allocated;
	ElfImage *img, *new;

	if(slot == nil)
		panic("image_attach: nil slot, pc %#p", getcallerpc());
	if(c == nil)
		panic("image_attach: nil chan, pc %#p", getcallerpc());
	if(segments == nil)
		panic("image_attach: nil segments, pc %#p", getcallerpc());

	rlock(&pool_lock);

	allocated = pool.allocated;	/* save it as it might change when we release the rlock */

	/* search for a known image */
	img = nil;
	chain = &pool.hash[img_hash(c->qid.path)];
	while(*chain != 0){
		img = img_get(*chain);
		lock(&img->l);
		if(eqqid(c->qid, img->qid) &&
		   c->mqid.path == img->mpath &&
		   c->mchan == img->mchan &&
		   c->dev->dc == img->dc) {
			if(incref(&img->r) == 1){
				/* img was sleeping:
				 * since we do not hold the pool's wlock
				 * we do not update the free list now,
				 * just the counter.
				 */
				adec(&pool.sleeping);
			}
			unlock(&img->l);
			runlock(&pool_lock);
			*slot = *chain;
			return 1;
		}
		unlock(&img->l);
		chain = &img->hnext;
	}
	if((blankslot = adec(&pool.blanks)) > 0){
		/* we do not need a wlock(&pool_lock) as far as we work with
		 * blank images: ainc and cas16 are enough
		 */
UseBlank:
		newp = allocated - blankslot;
		new = img_get(newp);
		lock(&new->l);	/* everybody else must wait */
		image_initialize(new, c);
		if(!segments_fill(new->seg, segments)){
			/* not enough memory (or bad segments):
			 * in theory we could:
			 * - run a garbage collection, to get more memory
			 *   and try again
			 * - search the freelist for an existing image
			 *   large enough to be recycled and use it
			 *
			 * however the caller could be out of luck anyway
			 * if another process use the freed pages,
			 * thus forget false hopes and fail fast.
			 */
			image_unfill(new);
			unlock(&new->l);
			runlock(&pool_lock);
			freelist_add(newp, new);
			return 0;
		}
		while(!cas16(chain, 0, newp)){
			img = img_get(*chain);
			chain = &img->hnext;
		}
		unlock(&new->l);
		runlock(&pool_lock);
		*slot = newp;
		return 1;
	}

	/* no more blanks... do we have a sleeping image to recycle? */
	runlock(&pool_lock);
	if((newp = freelist_pop(blankslot, allocated)) != 0){
		/* we got an image to recycle! */

		rlock(&pool_lock);
		new = img_get(newp);
		lock(&new->l);

		/* we have to:
		 * - free pages
		 * - resize the page tables in the segments
		 *   (which could fail, in which case we table_free
		 *   the page tables, re-add the image to the free list
		 *   and return 0)
		 * - update the segments info
		 * - update the image info
		 * - add the image to the appropriate hash chain
		 * - return 1
		 */
		if(!segments_fill(new->seg, segments)){
			unlock(&new->l);
			runlock(&pool_lock);
			freelist_add(newp, new);
			return 0;
		}
		image_initialize(new, c);
		while(!cas16(chain, 0, newp)){
			img = img_get(*chain);
			chain = &img->hnext;
		}

		unlock(&new->l);
		runlock(&pool_lock);
		return 1;
	}

	/* we are really unlucky: neither blanks nor free pages:
	 * we have to add a new chunck to the pool
	 */
	if(!imagepool_grow()){
		if(pool.allocated <= allocated - blankslot){
			/* the pool did NOT grow enougth for us */
			return 0;
		}
		/* despite the failure, the pool did grow enougth for us */
	}
	rlock(&pool_lock);
	goto UseBlank;
}

static void
faultio_error(Chan *c)
{
	char buf[ERRMAX];

	if(c && c->path && c->path->s)
		jehanne_snprint(buf, sizeof buf, "%s accessing %s: %s", Eioload, c->path->s, up->errstr);
	else
		jehanne_snprint(buf, sizeof buf, "sys: %s", Eioload);

	if(up->nerrlab) {
		postnote(up, 1, buf, NDebug);
		error(buf);
	}
	pexit(buf, 1);
}

/* Fill *page with the image page corresponding to va if it's empty.
 *
 * Returns 1 if the image page is in *page at exit, 0 otherwise.
 */
int
image_fill(PagePointer* page, ElfSegPointer segment, uintptr_t va)
{
	Chan *c;
	ElfImage* img;
	ElfSegment *seg;
	PagePointer *tmp, new;
	char *kaddr, *pstart;
	uintptr_t memoryoff, rstart, rend;
	int n;
	ImagePointer ptr;

	if(page == nil)
		panic("image_fill: nil page, pc %#p", getcallerpc());
	if(segment == 0)
		panic("image_fill: uninitialized segment pointer, pc %#p", getcallerpc());
	if(va&(PGSZ-1)){
		/* image_fill is called only from
		 * - segment_fault_text
		 * - segment_fault_data
		 * and both receive an aligned va from segment_fault
		 */
		panic("image_fill: misaligned virtual address, pc %#p", getcallerpc());
	}
	ptr = segment / NLOAD;
	rlock(&pool_lock);
	if(ptr > pool.allocated - pool.blanks)
		panic("image_fill: image out of pool, pc %#p", getcallerpc());
	img = img_get(ptr);
	if(img->r.ref == 0)
		panic("image_fill: lookup of free image, pc %#p", getcallerpc());
	seg = &img->seg[segment % NLOAD];
	runlock(&pool_lock);

	/* Now that we have the segment, we do not need the lock anymore
	 */
	memoryoff = va - seg->pg0addr;
	tmp = &seg->pages[memoryoff >> PGSHFT];
	if(*tmp != 0)
		return page_assign(page, *tmp);
	new = 0;
	if(!page_new(&new, 0))
		return 0;

	kaddr = page_kmap(new);

	/* lets do the various calculations out of lock since we
	 * only read values that are not going to change
	 */
	if(memoryoff == 0){
		pstart = kaddr + seg->pg0off;
		rstart = seg->fstart;
		rend = (rstart + PGSZ)&~(PGSZ-1);
	} else {
		pstart = kaddr;
		rstart = (seg->fstart + memoryoff)&~(PGSZ-1);
		rend = rstart + PGSZ;
	}
	if(rend > seg->fend)
		rend = seg->fend;

	/* Lets clear
	 * - the first page of a misaligned segment (rstart & (PGSZ-1))
	 * - the last page of the segment, if partial (rend & (PGSZ-1))
	 */
	if ((rend & (PGSZ-1)) || (rstart & (PGSZ-1)))
		jehanne_memset(kaddr, 0, PGSZ);

	c = img->c;

	qlock(&seg->l);
	if(*tmp != 0 || *page != 0){
		qunlock(&seg->l);
		page_kunmap(new, &kaddr);
		page_dispose(&new);
		MLG("page %#p new %d pa %#p page known *tmp %hd *page %hd", page, *tmp, page_pa(*tmp), *page, *tmp);
		return page_assign(page, *tmp);
	}

	/* ok, so we really have to read the file */
	while(waserror()){
		if(jehanne_strcmp(up->errstr, Eintr) == 0)
			continue;
		qunlock(&seg->l);
		page_kunmap(new, &kaddr);
		page_dispose(&new);
		faultio_error(c);
	}

	/* NOTE that we need faultio_error/waserror here because
	 * any 9p Rerror would be dispatced via error by mountrpc
	 * (see devmnt.c)
	 */
	n = c->dev->read(c, pstart, rend-rstart, rstart);
	if(n != rend-rstart)
		faultio_error(c);

	poperror();
	page_kunmap(new, &kaddr);

	*tmp = new;

	ainc(&img->pcount);
	qunlock(&seg->l);

	MLG("page %#p new %d pa %#p data_amount %hd data_offset %#p", page, *tmp, page_pa(*tmp), data_amount, data_offset);
	return page_assign(page, *tmp);
}

int
image_known(ElfSegPointer ptr)
{
	int known;
	rlock(&pool_lock);
	known = ptr == -1 || (ptr > 0 && ptr < pool.allocated - pool.blanks);
	runlock(&pool_lock);
	return known;
}

void
image_release(ImagePointer ptr)
{
	ElfImage *img;
	if(ptr == 0)
		panic("image_release: uninitialized image pointer, pc %#p", getcallerpc());
	img = img_get(ptr);
	if(decref(&img->r) > 0)
		return;
	freelist_add(ptr, img);
}

void
elfsegment_assign(ElfSegPointer *target, ElfSegPointer ptr)
{
	ElfImage *img;
	if(target == nil)
		panic("elfsegment_assign: nil target, pc %#p", getcallerpc());
	if(ptr == 0)
		panic("elfsegment_assign: uninitialized segment pointer, pc %#p", getcallerpc());
	img = img_get(ptr/NLOAD);
	incref(&img->r);
	*target = ptr;
}
