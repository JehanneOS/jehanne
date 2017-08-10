/*
 * Size memory and create the kernel page-tables on the fly while doing so.
 * Called from main(), this code should only be run by the bootstrap processor.
 *
 * MemMin is what the bootstrap code in entry.S has already mapped;
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "acpi.h"

#define PADDRP(va) PADDR(UINT2PTR(va))

uint32_t	MemMin = 0x1;		/* forced in data, set by entry.S */

#define MEMDEBUG	0

enum {
	MemUPA		= 0,		/* unbacked physical address */
	MemRAM		= 1,		/* physical memory */
	MemUMB		= 2,		/* upper memory block (<16MB) */
	MemReserved	= 3,
	NMemType	= 4,

	KB		= 1024,
};

typedef struct Map Map;
struct Map {
	uintptr_t	size;
	uintptr_t	addr;
};

typedef struct RMap RMap;
struct RMap {
	char*	name;
	Map*	map;
	Map*	mapend;

	Lock	l;
};

/*
 * Memory allocation tracking.
 */
static Map mapupa[64];
static RMap rmapupa = {
	"unallocated unbacked physical memory",
	mapupa,
	&mapupa[nelem(mapupa)-1],
};

static Map mapram[16];
RMap rmapram = {
	"physical memory",
	mapram,
	&mapram[nelem(mapram)-1],
};

static Map mapumb[64];
static RMap rmapumb = {
	"upper memory block",
	mapumb,
	&mapumb[nelem(mapumb)-1],
};

static Map mapumbrw[16];
static RMap rmapumbrw = {
	"UMB device memory",
	mapumbrw,
	&mapumbrw[nelem(mapumbrw)-1],
};

void
mapprint(RMap *rmap)
{
	Map *mp;

	print("%s\n", rmap->name);
	for(mp = rmap->map; mp->size; mp++)
		print("\t%#p %#p (%#p)\n", mp->addr, mp->addr+mp->size, mp->size);
}

/*
 * Notes:
 * asmmapinit and asmmodinit called from multiboot or e820;
 * subject to change; the numerology here is probably suspect.
 * Multiboot defines the alignment of modules as 4096.
 */
void
asmmapinit(uintmem addr, uintmem size, int type)
{
	if(type == AddrsMemory){
		/*
		 * Adjust things for the peculiarities of this
		 * architecture.
		 */
		if(addr < 1*MiB || addr+size < sys->pmstart)
			return;
		if(addr < sys->pmstart){
			size -= sys->pmstart - addr;
			addr = sys->pmstart;
		}
		mapfree(&rmapram, addr, size);
		sys->pmoccupied += size;
	}else if(specialmem != nil)
		specialmem(addr, size, type);
	mapfree(&rmapumbrw, addr, size);
}

void
asmmodinit(uint32_t start, uint32_t end, char* _1)
{
	if(start < sys->pmstart)
		return;
	end = ROUNDUP(end, 4096);
	if(end > sys->pmstart){
		mapalloc(&rmapram, sys->pmstart, end-sys->pmstart, 0);
		sys->pmstart = end;
	}
}

void
memaffinity(uint64_t base, uint64_t len, uint32_t dom, int flags)
{
	if(flags & MemNonVolatile){
		memreserve(base, len);
		return;
	}
	DBG("mem affinity: %#16.16llux %#16.16llux -> %d\n", base, base+len-1, dom);
	/* TO DO: add [base, base+len[ to dom table */
}

void
memdebug(void)
{
	unsigned long maxpa, maxpa1, maxpa2;

	maxpa = (nvramread(0x18)<<8)|nvramread(0x17);
	maxpa1 = (nvramread(0x31)<<8)|nvramread(0x30);
	maxpa2 = (nvramread(0x16)<<8)|nvramread(0x15);
	print("maxpa = %luX -> %luX, maxpa1 = %luX maxpa2 = %luX\n",
		maxpa, MB+maxpa*KB, maxpa1, maxpa2);

	mapprint(&rmapram);
	mapprint(&rmapumb);
	mapprint(&rmapumbrw);
	mapprint(&rmapupa);
}

void
mapfree(RMap* rmap, uintptr_t addr, uintptr_t size)
{
	Map *mp;
	uintptr_t t;

	if(size <= 0)
		return;

	ilock(&rmap->l);
	for(mp = rmap->map; mp->addr <= addr && mp->size; mp++)
		;

	if(mp > rmap->map && (mp-1)->addr+(mp-1)->size == addr){
		(mp-1)->size += size;
		if(addr+size == mp->addr){
			(mp-1)->size += mp->size;
			while(mp->size){
				mp++;
				(mp-1)->addr = mp->addr;
				(mp-1)->size = mp->size;
			}
		}
	}
	else{
		if(addr+size == mp->addr && mp->size){
			mp->addr -= size;
			mp->size += size;
		}
		else do{
			if(mp >= rmap->mapend){
				print("mapfree: %s: losing %#p, %#p\n",
					rmap->name, addr, size);
				break;
			}
			t = mp->addr;
			mp->addr = addr;
			addr = t;
			t = mp->size;
			mp->size = size;
			mp++;
		}while(size = t);
	}
	iunlock(&rmap->l);
}

void*
basealloc(usize nb, uint32_t align, usize *alloced)
{
	uintmem pa;

	if(align < CACHELINESZ)
		align = CACHELINESZ;
	nb = ROUNDUP(nb, CACHELINESZ);
	pa = mapalloc(&rmapram, 0, nb, align);
	if(pa == 0)
		return nil;
	if(alloced != nil)
		*alloced = nb;
	return KADDR(pa);
}

void
basefree(void *p, usize nb)
{
	uintmem pa;

	pa = PADDR(p);
	mapfree(&rmapram, pa, nb);
}

uintptr_t
mapalloc(RMap* rmap, uintptr_t addr, int size, int align)
{
	Map *mp;
	uintptr_t maddr, oaddr;

	ilock(&rmap->l);
	for(mp = rmap->map; mp->size; mp++){
		maddr = mp->addr;

		if(addr){
			/*
			 * A specific address range has been given:
			 *   if the current map entry is greater then
			 *   the address is not in the map;
			 *   if the current map entry does not overlap
			 *   the beginning of the requested range then
			 *   continue on to the next map entry;
			 *   if the current map entry does not entirely
			 *   contain the requested range then the range
			 *   is not in the map.
			 */
			if(maddr > addr)
				break;
			if(mp->size < addr - maddr)	/* maddr+mp->size < addr, but no overflow */
				continue;
			if(addr - maddr > mp->size - size)	/* addr+size > maddr+mp->size, but no overflow */
				break;
			maddr = addr;
		}

		if(align > 0)
			maddr = ((maddr+align-1)/align)*align;
		if(mp->addr+mp->size-maddr < size)
			continue;

		oaddr = mp->addr;
		mp->addr = maddr+size;
		mp->size -= maddr-oaddr+size;
		if(mp->size == 0){
			do{
				mp++;
				(mp-1)->addr = mp->addr;
			}while((mp-1)->size = mp->size);
		}

		iunlock(&rmap->l);
		if(oaddr != maddr)
			mapfree(rmap, oaddr, maddr-oaddr);

		return maddr;
	}
	iunlock(&rmap->l);

	return 0;
}

/*
 * Allocate from the ram map directly to make page tables.
 * Called by mmuwalk during e820scan.
 */
void*
rampage(void)
{
	uintptr_t m;

	m = mapalloc(&rmapram, 0, PGSZ, PGSZ);
	if(m == 0)
		return nil;
	return KADDR(m);
}

static void
umbexclude(void)
{
	int size;
	unsigned long addr;
	char *op, *p, *rptr;

	if((p = getconf("umbexclude")) == nil)
		return;

	while(p && *p != '\0' && *p != '\n'){
		op = p;
		addr = strtoul(p, &rptr, 0);
		if(rptr == nil || rptr == p || *rptr != '-'){
			print("umbexclude: invalid argument <%s>\n", op);
			break;
		}
		p = rptr+1;

		size = strtoul(p, &rptr, 0) - addr + 1;
		if(size <= 0){
			print("umbexclude: bad range <%s>\n", op);
			break;
		}
		if(rptr != nil && *rptr == ',')
			*rptr++ = '\0';
		p = rptr;

		mapalloc(&rmapumb, addr, size, 0);
	}
}

static void
umbscan(void)
{
	unsigned char *p;

	/*
	 * Scan the Upper Memory Blocks (0xA0000->0xF0000) for pieces
	 * which aren't used; they can be used later for devices which
	 * want to allocate some virtual address space.
	 * Check for two things:
	 * 1) device BIOS ROM. This should start with a two-byte header
	 *    of 0x55 0xAA, followed by a byte giving the size of the ROM
	 *    in 512-byte chunks. These ROM's must start on a 2KB boundary.
	 * 2) device memory. This is read-write.
	 * There are some assumptions: there's VGA memory at 0xA0000 and
	 * the VGA BIOS ROM is at 0xC0000. Also, if there's no ROM signature
	 * at 0xE0000 then the whole 64KB up to 0xF0000 is theoretically up
	 * for grabs; check anyway.
	 */
	p = KADDR(0xD0000);
	while(p < (unsigned char*)KADDR(0xE0000)){
		/*
		 * Test for 0x55 0xAA before poking obtrusively,
		 * some machines (e.g. Thinkpad X20) seem to map
		 * something dynamic here (cardbus?) causing weird
		 * problems if it is changed.
		 */
		if(p[0] == 0x55 && p[1] == 0xAA){
			p += p[2]*512;
			continue;
		}

		p[0] = 0xCC;
		p[2*KB-1] = 0xCC;
		if(p[0] != 0xCC || p[2*KB-1] != 0xCC){
			p[0] = 0x55;
			p[1] = 0xAA;
			p[2] = 4;
			if(p[0] == 0x55 && p[1] == 0xAA){
				p += p[2]*512;
				continue;
			}
			if(p[0] == 0xFF && p[1] == 0xFF)
				mapfree(&rmapumb, PADDRP(p), 2*KB);
		}
		else
			mapfree(&rmapumbrw, PADDRP(p), 2*KB);
		p += 2*KB;
	}

	p = KADDR(0xE0000);
	if(p[0] != 0x55 || p[1] != 0xAA){
		p[0] = 0xCC;
		p[64*KB-1] = 0xCC;
		if(p[0] != 0xCC && p[64*KB-1] != 0xCC)
			mapfree(&rmapumb, PADDRP(p), 64*KB);
	}

	umbexclude();
}

int
checksum(void *v, int n)
{
	unsigned char *p, s;

	s = 0;
	p = v;
	while(n-- > 0)
		s += *p++;
	return s;
}

static void*
sigscan(unsigned char* addr, int len, char* signature)
{
	int sl;
	unsigned char *e, *p;

	e = addr+len;
	sl = strlen(signature);
	for(p = addr; p+sl < e; p += 16)
		if(memcmp(p, signature, sl) == 0)
			return p;
	return nil;
}

static uintptr_t
convmemsize(void)
{
	uintptr_t top;
	unsigned char *bda;

	bda = KADDR(0x400);
	top = ((bda[0x14]<<8) | bda[0x13])*KB;

	if(top < 64*KB || top > 640*KB)
		top = 640*KB;	/* sanity */

	/* reserved for bios tables (EBDA) */
	top -= 1*KB;

	return top;
}

void*
sigsearch(char* signature)
{
	uintptr_t p;
	unsigned char *bda;
	void *r;

	/*
	 * Search for the data structure:
	 * 1) within the first KiB of the Extended BIOS Data Area (EBDA), or
	 * 2) within the last KiB of system base memory if the EBDA segment
	 *    is undefined, or
	 * 3) within the BIOS ROM address space between 0xf0000 and 0xfffff
	 *    (but will actually check 0xe0000 to 0xfffff).
	 */
	bda = KADDR(0x400);
	if(memcmp(KADDR(0xfffd9), "EISA", 4) == 0){
		if((p = (bda[0x0f]<<8)|bda[0x0e]) != 0){
			if((r = sigscan(KADDR(p<<4), 1024, signature)) != nil)
				return r;
		}
	}
	if((r = sigscan(KADDR(convmemsize()), 1024, signature)) != nil)
		return r;

	/* hack for virtualbox: look in KiB below 0xa0000 */
	if((r = sigscan(KADDR(0xa0000-1024), 1024, signature)) != nil)
		return r;

	return sigscan(KADDR(0xe0000), 0x20000, signature);
}

#if 0
static void
lowraminit(void)
{
	uintptr_t pa, x;

	/*
	 * Initialise the memory bank information for conventional memory
	 * (i.e. less than 640KB). The base is the first location after the
	 * bootstrap processor MMU information and the limit is obtained from
	 * the BIOS data area.
	 */
	x = PADDRP(CPU0END);
	pa = convmemsize();
	if(x < pa){
		mapfree(&rmapram, x, pa-x);
		memset(KADDR(x), 0, pa-x);		/* keep us honest */
	}

	x = PADDRP(PGROUND((uintptr_t)end));
	pa = MemMin;
	if(x > pa)
		panic("kernel too big");
	mapfree(&rmapram, x, pa-x);
	memset(KADDR(x), 0, pa-x);		/* keep us honest */
}
#endif

typedef struct Emap Emap;
struct Emap
{
	int type;
	uintptr_t base;
	uintptr_t top;
};
static Emap emap[128];
int nemap;

int
e820map(int type, uintptr_t base, uintptr_t top)
{
	if(nemap == sizeof(emap))
		return 0;
	emap[nemap].type = type;
	emap[nemap].base = base;
	emap[nemap].top = top;
	++nemap;
	return 1;
}

static int
emapcmp(void *va, void *vb)
{
	Emap *a, *b;

	a = (Emap*)va;
	b = (Emap*)vb;
	if(a->top < b->top)
		return -1;
	if(a->top > b->top)
		return 1;
	if(a->base < b->base)
		return -1;
	if(a->base > b->base)
		return 1;
	return 0;
}

int
ismapped(RMap *r, uintptr_t addr, uintptr_t *limit)
{
	Map *mp;

	ilock(&r->l);
	for(mp = r->map; mp->size; mp++){
		if(mp->addr <= addr && addr < mp->addr + mp->size){
			if(limit != nil)
				*limit = mp->addr + mp->size;
			iunlock(&r->l);
			return 1;
		}
	}
	iunlock(&r->l);
	return 0;
}

uintptr_t
mapsize(RMap *r)
{
	uintptr_t t;
	Map *e;

	ilock(&r->l);
	t = 0;
	for(e = r->map; e->size; e++)
		t += e->size;
	iunlock(&r->l);
	return t;
}

uintptr_t
maptop(RMap *r)
{
	uintptr_t t;
	Map *e;

	ilock(&r->l);
	t = 0;
	for(e = r->map; e->size; e++)
		t = e->addr + e->size;
	iunlock(&r->l);
	return t;
}

int
mapfirst(RMap *r, uintptr_t start, uintptr_t *addr, uintptr_t *size)
{
	Map *e;
	uintptr_t lim;

	ilock(&r->l);
	for(e = r->map; e->size; e++){
		lim = e->addr + e->size;
		if(e->addr <= start && start < lim){
			iunlock(&r->l);
			*addr = start;
			*size = lim-start;
			return 1;
		}
	}
	iunlock(&r->l);
	return 0;
}

static void
map(uintptr_t base, uintptr_t len, int type)
{
	uintptr_t n, flags, maxkpa;

	/*
	 * Split any call crossing MemMin to make below simpler.
	 */
	if(base < MemMin && len > MemMin-base){
		n = MemMin - base;
		map(base, n, type);
		map(MemMin, len-n, type);
	}

	/*
	 * Let umbscan hash out the low MemMin.
	 */
	if(base < MemMin)
		return;

	/*
	 * Any non-memory below 16*MB is used as upper mem blocks.
	 */
	if(type == MemUPA && base < 16*MB && len > 16*MB-base){
		map(base, 16*MB-base, MemUMB);
		map(16*MB, len-(16*MB-base), MemUPA);
		return;
	}

	/*
	 * Memory below CPU0END is reserved for the kernel
	 * and already mapped.
	 */
	if(base < PADDRP(CPU0END)){
		n = PADDRP(CPU0END) - base;
		if(len <= n)
			return;
		map(PADDRP(CPU0END), len-n, type);
		return;
	}

	/*
	 * Memory between KTZERO and end is the kernel itself
	 * and is already mapped.
	 */
	if(base < PADDRP(KTZERO) && len > PADDRP(KTZERO)-base){
		map(base, PADDRP(KTZERO)-base, type);
		return;
	}
	if(PADDRP(KTZERO) < base && base < PADDRP(PGROUND((uintptr_t)end))){
		n = PADDRP(PGROUND((uintptr_t)end));
		if(len <= n)
			return;
		map(PADDRP(PGROUND((uintptr_t)end)), len-n, type);
		return;
	}

	/*
	 * Now we have a simple case.
	 */
	switch(type){
	case MemRAM:
		mapfree(&rmapram, base, len);
		flags = PTEWRITE|PTEVALID;
		break;
	case MemUMB:
		mapfree(&rmapumb, base, len);
		flags = PTEWRITE|PTEUNCACHED|PTEVALID;
		break;
	case MemUPA:
		mapfree(&rmapupa, base, len);
		flags = 0;
		break;
	default:
	case MemReserved:
		flags = 0;
		break;
	}

	if(flags){
		maxkpa = -KZERO;
		if(base >= maxkpa)
			return;
		if(len > maxkpa-base)
			len = maxkpa - base;
		pmap(m->pml4, base|flags, base+KZERO, len);
	}
}

int
e820(void)
{
	uintptr_t base, len, last;
	Emap *e;
	char *s;
	int i;

	/* passed by bootloader */
	if((s = getconf("*e820")) == nil
	&& (s = getconf("e820")) == nil
	&& nemap == 0)
		return -1;
	while(s && nemap < nelem(emap)){
		while(*s == ' ')
			s++;
		if(*s == 0)
			break;
		e = emap + nemap;
		e->type = 1;
		if(s[1] == ' '){	/* new format */
			e->type = s[0] - '0';
			s += 2;
		}
		e->base = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		e->top  = strtoull(s, &s, 16);
		if(*s != ' ' && *s != 0)
			break;
		if(e->base < e->top)
			nemap++;
	}
	if(nemap == 0)
		return -1;
	qsort(emap, nemap, sizeof emap[0], emapcmp);
	last = 0;
	for(i=0; i<nemap; i++){
		e = &emap[i];
		/*
		 * pull out the info but only about the low 32 bits...
		 */
		if(e->top <= last)
			continue;
		if(e->base < last)
			base = last;
		else
			base = e->base;
		len = e->top - base;
		/*
		 * If the map skips addresses, mark them available.
		 */
		if(last < base)
			map(last, base-last, MemUPA);
		map(base, len, (e->type == 1) ? MemRAM : MemReserved);
		last = base + len;
		if(last == 0)
			break;
	}
	if(last != 0)
		map(last, -last, MemUPA);
	return 0;
}

void
meminit(void)
{
	int i;
	Map *mp;
	Confmem *cm;
	uintptr_t lost;

	umbscan();
//	lowraminit();
	e820();

	/*
	 * Set the conf entries describing banks of allocatable memory.
	 */
	for(i=0; i<nelem(mapram) && i<nelem(sys->mem); i++){
		mp = &rmapram.map[i];
		cm = &sys->mem[i];
		cm->base = mp->addr;
		cm->npage = mp->size/PGSZ;
	}

	lost = 0;
	for(; i<nelem(mapram); i++)
		lost += rmapram.map[i].size;
	if(lost)
		print("meminit - lost %llud bytes\n", lost);

	if(MEMDEBUG)
		memdebug();
}

/*
 * Allocate memory from the upper memory blocks.
 */
uintptr_t
umbmalloc(uintptr_t addr, int size, int align)
{
	uintptr_t a;

	if(a = mapalloc(&rmapumb, addr, size, align))
		return (uintptr_t)KADDR(a);

	return 0;
}

void
umbfree(uintptr_t addr, int size)
{
	mapfree(&rmapumb, PADDRP(addr), size);
}

uintptr_t
umbrwmalloc(uintptr_t addr, int size, int align)
{
	uintptr_t a;
	unsigned char *p;

	if(a = mapalloc(&rmapumbrw, addr, size, align))
		return (uintptr_t)KADDR(a);

	/*
	 * Perhaps the memory wasn't visible before
	 * the interface is initialised, so try again.
	 */
	if((a = umbmalloc(addr, size, align)) == 0)
		return 0;
	p = (unsigned char*)a;
	p[0] = 0xCC;
	p[size-1] = 0xCC;
	if(p[0] == 0xCC && p[size-1] == 0xCC)
		return a;
	umbfree(a, size);

	return 0;
}

void
umbrwfree(uintptr_t addr, int size)
{
	mapfree(&rmapumbrw, PADDRP(addr), size);
}

/*
 * Give out otherwise-unused physical address space
 * for use in configuring devices.  Note that upaalloc
 * does not map the physical address into virtual memory.
 * Call vmap to do that.
 */
uintptr_t
upaalloc(int size, int align)
{
	uintptr_t a;

	a = mapalloc(&rmapupa, 0, size, align);
	if(a == 0){
		print("out of physical address space allocating %d\n", size);
		mapprint(&rmapupa);
	}
	return a;
}

void
upafree(uintptr_t pa, int size)
{
	mapfree(&rmapupa, pa, size);
}

void
upareserve(uintptr_t pa, int size)
{
	uintptr_t a;

	a = mapalloc(&rmapupa, pa, size, 0);
	if(a != pa){
		/*
		 * This can happen when we're using the E820
		 * map, which might have already reserved some
		 * of the regions claimed by the pci devices.
		 */
	//	print("upareserve: cannot reserve pa=%#p size=%d\n", pa, size);
		if(a != 0)
			mapfree(&rmapupa, a, size);
	}
}

void
memreserve(uintmem pa, uintmem size)
{
	upareserve(pa, size);
}

void
memorysummary(void)
{
	memdebug();
}

