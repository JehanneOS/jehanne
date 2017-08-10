/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

Rectangle physgscreenr;

Memimage *gscreen;

VGAscr vgascreen[1];

int
screensize(int x, int y, int _, uint32_t chan)
{
	VGAscr *scr;

	qlock(&drawlock);
	if(waserror()){
		qunlock(&drawlock);
		nexterror();
	}

	if(memimageinit() < 0)
		error("memimageinit failed");

	lock(&vgascreenlock);
	if(waserror()){
		unlock(&vgascreenlock);
		nexterror();
	}

	scr = &vgascreen[0];
	scr->gscreendata = nil;
	scr->gscreen = nil;
	if(gscreen){
		freememimage(gscreen);
		gscreen = nil;
	}

	if(scr->paddr == 0){
		if(scr->dev && scr->dev->page){
			scr->vaddr = KADDR(VGAMEM());
			scr->apsize = 1<<16;
		}
		scr->softscreen = 1;
	}
	if(scr->softscreen){
		gscreen = allocmemimage(Rect(0,0,x,y), chan);
		scr->useflush = 1;
	}else{
		static Memdata md;

		md.ref = 1;
		if((md.bdata = scr->vaddr) == 0)
			error("framebuffer not maped");
		gscreen = allocmemimaged(Rect(0,0,x,y), chan, &md);
		scr->useflush = scr->dev && scr->dev->flush;
	}
	if(gscreen == nil)
		error("no memory for vga memimage");

	scr->palettedepth = 6;	/* default */
	scr->memdefont = getmemdefont();
	scr->gscreen = gscreen;
	scr->gscreendata = gscreen->data;

	physgscreenr = gscreen->r;

	vgaimageinit(chan);

	unlock(&vgascreenlock);
	poperror();

	drawcmap();
	swcursorinit();

	qunlock(&drawlock);
	poperror();

	return 0;
}

int
screenaperture(int size, int align)
{
	VGAscr *scr;

	scr = &vgascreen[0];

	if(scr->paddr)	/* set up during enable */
		return 0;

	if(size == 0)
		return 0;

	if(scr->dev && scr->dev->linear){
		scr->dev->linear(scr, size, align);
		return 0;
	}

	/*
	 * Need to allocate some physical address space.
	 * The driver will tell the card to use it.
	 */
	size = PGROUND(size);
	scr->paddr = upaalloc(size, align);
	if(scr->paddr == 0)
		return -1;
	scr->vaddr = vmap(scr->paddr, size);
	if(scr->vaddr == nil)
		return -1;
	scr->apsize = size;

	return 0;
}

uint8_t*
attachscreen(Rectangle* r, uint32_t* chan, int* d, int* width, int *softscreen)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(scr->gscreen == nil || scr->gscreendata == nil)
		return nil;

	*r = scr->gscreen->clipr;
	*chan = scr->gscreen->chan;
	*d = scr->gscreen->depth;
	*width = scr->gscreen->width;
	if(scr->gscreendata->allocd){
		/*
		 * we use a memimage as softscreen. devdraw will create its own
		 * screen image on the backing store of that image. when our gscreen
		 * and devdraws screenimage gets freed, the imagedata will
		 * be released.
		 */
		*softscreen = 0xa110c;
		scr->gscreendata->ref++;
	} else
		*softscreen = scr->useflush ? 1 : 0;
	return scr->gscreendata->bdata;
}

void
flushmemscreen(Rectangle r)
{
	VGAscr *scr;
	uint8_t *sp, *disp, *sdisp, *edisp;
	int y, len, incs, off, page;

	scr = &vgascreen[0];
	if(scr->gscreen == nil || scr->useflush == 0)
		return;
	if(rectclip(&r, scr->gscreen->r) == 0)
		return;
	if(scr->dev && scr->dev->flush){
		scr->dev->flush(scr, r);
		return;
	}
	disp = scr->vaddr;
	incs = scr->gscreen->width*sizeof(uint32_t);
	off = (r.min.x*scr->gscreen->depth) / 8;
	len = (r.max.x*scr->gscreen->depth + 7) / 8;
	len -= off;
	off += r.min.y*incs;
	sp = scr->gscreendata->bdata + scr->gscreen->zero + off;

	/*
	 * Linear framebuffer with softscreen.
	 */
	if(scr->paddr){
		sdisp = disp+off;
		for(y = r.min.y; y < r.max.y; y++) {
			memmove(sdisp, sp, len);
			sp += incs;
			sdisp += incs;
		}
		return;
	}

	/*
	 * Paged framebuffer window.
	 */
	if(scr->dev == nil || scr->dev->page == nil)
		return;

	page = off/scr->apsize;
	off %= scr->apsize;
	sdisp = disp+off;
	edisp = disp+scr->apsize;

	scr->dev->page(scr, page);
	for(y = r.min.y; y < r.max.y; y++) {
		if(sdisp + incs < edisp) {
			memmove(sdisp, sp, len);
			sp += incs;
			sdisp += incs;
		}
		else {
			off = edisp - sdisp;
			page++;
			if(off <= len){
				if(off > 0)
					memmove(sdisp, sp, off);
				scr->dev->page(scr, page);
				if(len - off > 0)
					memmove(disp, sp+off, len - off);
			}
			else {
				memmove(sdisp, sp, len);
				scr->dev->page(scr, page);
			}
			sp += incs;
			sdisp += incs - scr->apsize;
		}
	}
}

void
getcolor(uint32_t p, uint32_t* pr, uint32_t* pg, uint32_t* pb)
{
	VGAscr *scr;
	uint32_t x;

	scr = &vgascreen[0];
	if(scr->gscreen == nil)
		return;

	switch(scr->gscreen->depth){
	default:
		x = 0x0F;
		break;
	case 8:
		x = 0xFF;
		break;
	}
	p &= x;

	lock(&cursor);
	*pr = scr->colormap[p][0];
	*pg = scr->colormap[p][1];
	*pb = scr->colormap[p][2];
	unlock(&cursor);
}

int
setpalette(uint32_t p, uint32_t r, uint32_t g, uint32_t b)
{
	VGAscr *scr;
	int d;

	scr = &vgascreen[0];
	d = scr->palettedepth;

	lock(&cursor);
	scr->colormap[p][0] = r;
	scr->colormap[p][1] = g;
	scr->colormap[p][2] = b;
	vgao(PaddrW, p);
	vgao(Pdata, r>>(32-d));
	vgao(Pdata, g>>(32-d));
	vgao(Pdata, b>>(32-d));
	unlock(&cursor);

	return ~0;
}

/*
 * On some video cards (e.g. Mach64), the palette is used as the 
 * DAC registers for >8-bit modes.  We don't want to set them when the user
 * is trying to set a colormap and the card is in one of these modes.
 */
int
setcolor(uint32_t p, uint32_t r, uint32_t g, uint32_t b)
{
	VGAscr *scr;
	int x;

	scr = &vgascreen[0];
	if(scr->gscreen == nil)
		return 0;

	switch(scr->gscreen->depth){
	case 1:
	case 2:
	case 4:
		x = 0x0F;
		break;
	case 8:
		x = 0xFF;
		break;
	default:
		return 0;
	}
	p &= x;

	return setpalette(p, r, g, b);
}

void
swenable(VGAscr* _)
{
	swcursorload(&arrow);
}

void
swdisable(VGAscr* _)
{
}

void
swload(VGAscr* _, Cursor *curs)
{
	swcursorload(curs);
}

int
swmove(VGAscr* _, Point p)
{
	swcursorhide();
	swcursordraw(p);
	return 0;
}

VGAcur swcursor =
{
	"soft",
	swenable,
	swdisable,
	swload,
	swmove,
};

VGAcur vgavesacur =
{
	"soft",
	swenable,
	swdisable,
	swload,
	swmove,
};

void
cursoron(void)
{
	VGAscr *scr;
	VGAcur *cur;

	scr = &vgascreen[0];
	cur = scr->cur;
	if(cur == nil || cur->move == nil)
		return;

	if(cur == &swcursor)
		qlock(&drawlock);
	lock(&cursor);
	cur->move(scr, mousexy());
	unlock(&cursor);
	if(cur == &swcursor)
		qunlock(&drawlock);
}

void
cursoroff(void)
{
}

void
setcursor(Cursor* curs)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(scr->cur == nil || scr->cur->load == nil)
		return;

	scr->cur->load(scr, curs);
}

int hwaccel = 0;
int hwblank = 0;
int panning = 0;

int
hwdraw(Memdrawparam *par)
{
	VGAscr *scr;
	Memimage *dst, *src, *mask;
	Memdata *scrd;
	int m;

	scr = &vgascreen[0];
	scrd = scr->gscreendata;
	if(scr->gscreen == nil || scrd == nil)
		return 0;
	if((dst = par->dst) == nil || dst->data == nil)
		return 0;
	if((src = par->src) && src->data == nil)
		src = nil;
	if((mask = par->mask) && mask->data == nil)
		mask = nil;
	if(scr->cur == &swcursor){
		if(dst->data->bdata == scrd->bdata)
			swcursoravoid(par->r);
		if(src && src->data->bdata == scrd->bdata)
			swcursoravoid(par->sr);
		if(mask && mask->data->bdata == scrd->bdata)
			swcursoravoid(par->mr);
	}
	if(!hwaccel || scr->softscreen)
		return 0;
	if(dst->data->bdata != scrd->bdata || src == nil || mask == nil)
		return 0;

	/*
	 * If we have an opaque mask and source is one opaque
	 * pixel we can convert to the destination format and just
	 * replicate with memset.
	 */
	m = Simplesrc|Simplemask|Fullmask;
	if(scr->fill
	&& (par->state&m)==m
	&& ((par->srgba&0xFF) == 0xFF)
	&& (par->op&S) == S)
		return scr->fill(scr, par->r, par->sdval);

	/*
	 * If no source alpha, an opaque mask, we can just copy the
	 * source onto the destination.  If the channels are the same and
	 * the source is not replicated, memmove suffices.
	 */
	m = Simplemask|Fullmask;
	if(scr->scroll
	&& src->data->bdata==dst->data->bdata
	&& !(src->flags&Falpha)
	&& (par->state&m)==m
	&& (par->op&S) == S)
		return scr->scroll(scr, par->r, par->sr);

	return 0;	
}

void
blankscreen(int blank)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(hwblank){
		if(scr->blank)
			scr->blank(scr, blank);
		else
			vgablank(scr, blank);
	}
}

static char*
vgalinearaddr0(VGAscr *scr, uint32_t paddr, int size)
{
	int x, nsize;
	uint32_t npaddr;

	/*
	 * new approach.  instead of trying to resize this
	 * later, let's assume that we can just allocate the
	 * entire window to start with.
	 */
	if(scr->paddr == paddr && size <= scr->apsize)
		return nil;

	if(scr->paddr){
		/*
		 * could call vunmap and vmap,
		 * but worried about dangling pointers in devdraw
		 */
		return "cannot grow vga frame buffer";
	}

	/* round to page boundary, just in case */
	x = paddr&(BY2PG-1);
	npaddr = paddr-x;
	nsize = PGROUND(size+x);

	/*
	 * Don't bother trying to map more than 4000x4000x32 = 64MB.
	 * We only have a 256MB window.
	 */
	if(nsize > 64*MB)
		nsize = 64*MB;
	scr->vaddr = vmap(npaddr, nsize);
	if(scr->vaddr == 0)
		return "cannot allocate vga frame buffer";

	patwc(scr->vaddr, nsize);

	scr->vaddr = (char*)scr->vaddr+x;
	scr->paddr = paddr;
	scr->apsize = nsize;

	mtrr(npaddr, nsize, "wc");

	return nil;
}

static char*
vgalinearpci0(VGAscr *scr)
{
	uint32_t paddr;
	int i, size, best;
	Pcidev *p;
	
	p = scr->pci;

	/*
	 * Scan for largest memory region on card.
	 * Some S3 cards (e.g. Savage) have enormous
	 * mmio regions (but even larger frame buffers).
	 * Some 3dfx cards (e.g., Voodoo3) have mmio
	 * buffers the same size as the frame buffer,
	 * but only the frame buffer is marked as
	 * prefetchable (bar&8).  If a card doesn't fit
	 * into these heuristics, its driver will have to
	 * call vgalinearaddr directly.
	 */
	best = -1;
	for(i=0; i<nelem(p->mem); i++){
		if(p->mem[i].bar&1)	/* not memory */
			continue;
		if(p->mem[i].size < 640*480)	/* not big enough */
			continue;
		if(best==-1 
		|| p->mem[i].size > p->mem[best].size 
		|| (p->mem[i].size == p->mem[best].size 
		  && (p->mem[i].bar&8)
		  && !(p->mem[best].bar&8)))
			best = i;
	}
	if(best >= 0){
		paddr = p->mem[best].bar & ~0x0F;
		size = p->mem[best].size;
		return vgalinearaddr0(scr, paddr, size);
	}
	return "no video memory found on pci card";
}

void
vgalinearpci(VGAscr *scr)
{
	char *err;

	if(scr->pci == nil)
		return;
	if((err = vgalinearpci0(scr)) != nil)
		error(err);
}

void
vgalinearaddr(VGAscr *scr, uint32_t paddr, int size)
{
	char *err;

	if((err = vgalinearaddr0(scr, paddr, size)) != nil)
		error(err);
}

static char*
bootmapfb(VGAscr *scr, uint32_t pa, uint32_t sz)
{
	uint32_t start, end;
	Pcidev *p;
	int i;

	for(p = pcimatch(nil, 0, 0); p != nil; p = pcimatch(p, 0, 0)){
		for(i=0; i<nelem(p->mem); i++){
			if(p->mem[i].bar & 1)
				continue;
			start = p->mem[i].bar & ~0xF;
			end = start + p->mem[i].size;
			if(pa == start && (pa + sz) <= end){
				scr->pci = p;
				return vgalinearpci0(scr);
			}
		}
	}
	return vgalinearaddr0(scr, pa, sz);
}

char*
rgbmask2chan(char *buf, int depth, uint32_t rm, uint32_t gm, uint32_t bm)
{
	uint32_t m[4], dm;	/* r,g,b,x */
	char tmp[32];
	int c, n;

	dm = 1<<depth-1;
	dm |= dm-1;

	m[0] = rm & dm;
	m[1] = gm & dm;
	m[2] = bm & dm;
	m[3] = (~(m[0] | m[1] | m[2])) & dm;

	buf[0] = 0;
Next:
	for(c=0; c<4; c++){
		for(n = 0; m[c] & (1<<n); n++)
			;
		if(n){
			m[0] >>= n, m[1] >>= n, m[2] >>= n, m[3] >>= n;
			snprint(tmp, sizeof tmp, "%c%d%s", "rgbx"[c], n, buf);
			strcpy(buf, tmp);
			goto Next;
		}
	}
	return buf;
}

/*
 * called early on boot to attach to framebuffer
 * setup by bootloader/firmware or plan9.
 */
void
bootscreeninit(void)
{
	static Memdata md;
	VGAscr *scr;
	int x, y, z;
	uint32_t chan, pa, sz;
	char *s, *p, *err;

	/* *bootscreen=WIDTHxHEIGHTxDEPTH CHAN PA [SZ] */
	s = getconf("*bootscreen");
	if(s == nil)
		return;

	x = strtoul(s, &s, 0);
	if(x == 0 || *s++ != 'x')
		return;

	y = strtoul(s, &s, 0);
	if(y == 0 || *s++ != 'x')
		return;

	z = strtoul(s, &s, 0);
	if(*s != ' ')
		return;
	if((p = strchr(++s, ' ')) == nil)
		return;
	*p = 0;
	chan = strtochan(s);
	*p = ' ';
	if(chan == 0 || chantodepth(chan) != z)
		return;

	sz = 0;
	pa = strtoul(p+1, &s, 0);
	if(pa == 0)
		return;
	if(*s++ == ' ')
		sz = strtoul(s, nil, 0);
	if(sz < x * y * (z+7)/8)
		sz = x * y * (z+7)/8;

	/* map framebuffer */
	scr = &vgascreen[0];
	if((err = bootmapfb(scr, pa, sz)) != nil){
		print("bootmapfb: %s\n", err);
		return;
	}

	if(memimageinit() < 0)
		return;

	md.ref = 1;
	md.bdata = scr->vaddr;
	gscreen = allocmemimaged(Rect(0,0,x,y), chan, &md);
	if(gscreen == nil)
		return;

	scr->palettedepth = 6;	/* default */
	scr->memdefont = getmemdefont();
	scr->gscreen = gscreen;
	scr->gscreendata = gscreen->data;
	scr->softscreen = 0;
	scr->useflush = 0;
	scr->dev = nil;

	physgscreenr = gscreen->r;

	vgaimageinit(chan);
	vgascreenwin(scr);

	/* turn mouse cursor on */
	swcursorinit();
	scr->cur = &swcursor;
	scr->cur->enable(scr);
	cursoron();

	sys->monitor = 1;
}

/*
 * called from devvga when the framebuffer is setup
 * to set *bootscreen= that can be passed on to a
 * new kernel on reboot.
 */
void
bootscreenconf(VGAscr *scr)
{
	char conf[100], chan[30];

	conf[0] = '\0';
	if(scr != nil && scr->paddr != 0 && scr->gscreen != nil)
		snprint(conf, sizeof(conf), "%dx%dx%d %s %#p %d\n",
			scr->gscreen->r.max.x, scr->gscreen->r.max.y,
			scr->gscreen->depth, chantostr(chan, scr->gscreen->chan),
			scr->paddr, scr->apsize);
	ksetenv("*bootscreen", conf, 1);
}
