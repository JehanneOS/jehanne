/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * VGA controller
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Qdir,
	Qvgabios,
	Qvgactl,
	Qvgaovl,
	Qvgaovlctl,
};

static Dirtab vgadir[] = {
	".",	{ Qdir, 0, QTDIR },		0,	0550,
	"vgabios",	{ Qvgabios, 0 },	0x100000, 0440,
	"vgactl",		{ Qvgactl, 0 },		0,	0660,
	"vgaovl",		{ Qvgaovl, 0 },		0,	0660,
	"vgaovlctl",	{ Qvgaovlctl, 0 },	0, 	0660,
};

enum {
	CMactualsize,
	CMdrawinit,
	CMhwaccel,
	CMhwblank,
	CMhwgc,
	CMlinear,
	CMpalettedepth,
	CMpanning,
	CMsize,
	CMtextmode,
	CMtype,
	CMsoftscreen,
	CMpcidev,
};

static Cmdtab vgactlmsg[] = {
	CMactualsize,	"actualsize",	2,
	CMdrawinit,	"drawinit",	1,
	CMhwaccel,	"hwaccel",	2,
	CMhwblank,	"hwblank",	2,
	CMhwgc,		"hwgc",		2,
	CMlinear,	"linear",	0,
	CMpalettedepth,	"palettedepth",	2,
	CMpanning,	"panning",	2,
	CMsize,		"size",		3,
	CMtextmode,	"textmode",	1,
	CMtype,		"type",		2,
	CMsoftscreen,	"softscreen",	2,
	CMpcidev,	"pcidev",	2,
};

static void
vgareset(void)
{
	Pcidev *pci;
	VGAscr *scr;

	/* reserve the 'standard' vga registers */
	if(ioalloc(0x2b0, 0x2df-0x2b0+1, 0, "vga") < 0)
		panic("vga ports already allocated");
	if(ioalloc(0x3c0, 0x3da-0x3c0+1, 0, "vga") < 0)
		panic("vga ports already allocated");

	/* find graphics card pci device */
	scr = &vgascreen[0];
	scr->pci = pci = nil;
	while((pci = pcimatch(pci, 0, 0)) != nil){
		if(pci->ccrb == Pcibcdisp){
			scr->pci = pci;
			break;
		}
	}

	sys->monitor = 1;
}

static Chan*
vgaattach(Chan *c, Chan *ac, char *spec, int flags)
{
	if(*spec && strcmp(spec, "0"))
		error(Eio);
	return devattach('v', spec);
}

Walkqid*
vgawalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, vgadir, nelem(vgadir), devgen);
}

static long
vgastat(Chan* c, uint8_t* dp, long n)
{
	return devstat(c, dp, n, vgadir, nelem(vgadir), devgen);
}

static Chan*
vgaopen(Chan* c, unsigned long omode)
{
	VGAscr *scr;
	static char *openctl = "openctl\n";

	scr = &vgascreen[0];
	if ((uint32_t)c->qid.path == Qvgaovlctl) {
		if (scr->dev && scr->dev->ovlctl)
			scr->dev->ovlctl(scr, c, openctl, strlen(openctl));
		else
			error(Enonexist);
	}
	return devopen(c, omode, vgadir, nelem(vgadir), devgen);
}

static void
vgaclose(Chan* c)
{
	VGAscr *scr;
	static char *closectl = "closectl\n";

	scr = &vgascreen[0];
	if((uint32_t)c->qid.path == Qvgaovlctl)
		if(scr->dev && scr->dev->ovlctl){
			if(waserror()){
				print("ovlctl error: %s\n", up->errstr);
				return;
			}
			scr->dev->ovlctl(scr, c, closectl, strlen(closectl));
			poperror();
		}
}

static long
vgaread(Chan* c, void* a, long n, int64_t off)
{
	char *p, *s, *e;
	VGAscr *scr;
	uint32_t offset = off;
	char chbuf[30];

	switch((uint32_t)c->qid.path){

	case Qdir:
		return devdirread(c, a, n, vgadir, nelem(vgadir), devgen);

	case Qvgabios:
		if(offset >= 0x100000)
			return 0;
		if(offset+n >= 0x100000)
			n = 0x100000 - offset;
		memmove(a, (uint8_t*)KADDR(0)+offset, n);
		return n;

	case Qvgactl:
		scr = &vgascreen[0];

		s = smalloc(READSTR);
		if(waserror()){
			free(s);
			nexterror();
		}
		p = s, e = s+READSTR;
		p = seprint(p, e, "type %s\n",
			scr->dev != nil ? scr->dev->name : "cga");
		if(scr->gscreen != nil) {
			p = seprint(p, e, "size %dx%dx%d %s\n",
				scr->gscreen->r.max.x, scr->gscreen->r.max.y,
				scr->gscreen->depth, chantostr(chbuf, scr->gscreen->chan));
			if(Dx(scr->gscreen->r) != Dx(physgscreenr)
			|| Dy(scr->gscreen->r) != Dy(physgscreenr))
				p = seprint(p, e, "actualsize %dx%d\n",
					physgscreenr.max.x, physgscreenr.max.y);
		}
		p = seprint(p, e, "hwgc %s\n",
			scr->cur != nil ? scr->cur->name : "off");
		p = seprint(p, e, "hwaccel %s\n", hwaccel ? "on" : "off");
		p = seprint(p, e, "hwblank %s\n", hwblank ? "on" : "off");
		p = seprint(p, e, "panning %s\n", panning ? "on" : "off");
		p = seprint(p, e, "addr p %#p v %#p size %#ux\n",
			scr->paddr, scr->vaddr, scr->apsize);
		p = seprint(p, e, "softscreen %s\n", scr->softscreen ? "on" : "off");
		USED(p);
		n = readstr(offset, a, n, s);
		poperror();
		free(s);

		return n;

	case Qvgaovl:
	case Qvgaovlctl:
		error(Ebadusefd);
		break;

	default:
		error(Egreg);
		break;
	}

	return 0;
}

//static char Ebusy[] = "vga already configured";

static void
vgactl(Cmdbuf *cb)
{
	int align, i, size, x, y, z;
	char *chanstr, *p;
	uint32_t chan;
	Cmdtab *ct;
	VGAscr *scr;
	extern VGAdev *vgadev[];
	extern VGAcur *vgacur[];

	scr = &vgascreen[0];
	ct = lookupcmd(cb, vgactlmsg, nelem(vgactlmsg));
	switch(ct->index){
	case CMhwgc:
		if(scr->gscreen == nil)
			error("hwgc: no gscreen");

		if(strcmp(cb->f[1], "off") == 0){
			lock(&cursor);
			if(scr->cur){
				if(scr->cur->disable)
					scr->cur->disable(scr);
				scr->cur = nil;
			}
			unlock(&cursor);
			return;
		}
		if(strcmp(cb->f[1], "soft") == 0){
			lock(&cursor);
			swcursorinit();
			if(scr->cur && scr->cur->disable)
				scr->cur->disable(scr);
			scr->cur = &swcursor;
			if(scr->cur->enable)
				scr->cur->enable(scr);
			unlock(&cursor);
			return;
		}
		for(i = 0; vgacur[i]; i++){
			if(strcmp(cb->f[1], vgacur[i]->name))
				continue;
			lock(&cursor);
			if(scr->cur && scr->cur->disable)
				scr->cur->disable(scr);
			scr->cur = vgacur[i];
			if(scr->cur->enable)
				scr->cur->enable(scr);
			unlock(&cursor);
			return;
		}
		break;

	case CMpcidev:
		if(cb->nf == 2){
			Pcidev *p;

			if((p = pcimatchtbdf(strtoul(cb->f[1], 0, 16))) != nil)
				scr->pci = p;
		} else
			error(Ebadarg);
		return;

	case CMtype:
		for(i = 0; vgadev[i]; i++){
			if(strcmp(cb->f[1], vgadev[i]->name))
				continue;
			if(scr->dev){
				qlock(&drawlock);
				scr->fill = nil;
				scr->scroll = nil;
				scr->blank = nil;
				hwblank = 0;
				hwaccel = 0;
				qunlock(&drawlock);
				if(scr->dev->disable)
					scr->dev->disable(scr);
			}
			scr->dev = vgadev[i];
			if(scr->dev->enable)
				scr->dev->enable(scr);
			return;
		}
		break;

	case CMtextmode:
		screen_init();
		bootscreenconf(nil);
		return;

	case CMsize:
		x = strtoul(cb->f[1], &p, 0);
		if(x == 0 || x > 10240)
			error(Ebadarg);
		if(*p)
			p++;

		y = strtoul(p, &p, 0);
		if(y == 0 || y > 10240)
			error(Ebadarg);
		if(*p)
			p++;

		z = strtoul(p, &p, 0);

		chanstr = cb->f[2];
		if((chan = strtochan(chanstr)) == 0)
			error("bad channel");

		if(chantodepth(chan) != z)
			error("depth, channel do not match");

		cursoroff();
		deletescreenimage();
		if(screensize(x, y, z, chan))
			error(Egreg);
		bootscreenconf(scr);
		return;

	case CMactualsize:
		if(scr->gscreen == nil)
			error("set the screen size first");

		x = strtoul(cb->f[1], &p, 0);
		if(x == 0 || x > 2048)
			error(Ebadarg);
		if(*p)
			p++;

		y = strtoul(p, nil, 0);
		if(y == 0 || y > 2048)
			error(Ebadarg);

		if(x > scr->gscreen->r.max.x || y > scr->gscreen->r.max.y)
			error("physical screen bigger than virtual");

		physgscreenr = Rect(0,0,x,y);
		scr->gscreen->clipr = physgscreenr;
		return;

	case CMpalettedepth:
		x = strtoul(cb->f[1], &p, 0);
		if(x != 8 && x != 6)
			error(Ebadarg);

		scr->palettedepth = x;
		return;

	case CMsoftscreen:
		if(strcmp(cb->f[1], "on") == 0)
			scr->softscreen = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			scr->softscreen = 0;
		else
			break;
		if(scr->gscreen == nil)
			return;
		x = scr->gscreen->r.max.x;
		y = scr->gscreen->r.max.y;
		z = scr->gscreen->depth;
		chan = scr->gscreen->chan;
		cursoroff();
		deletescreenimage();
		if(screensize(x, y, z, chan))
			error(Egreg);
		/* fall through */
	case CMdrawinit:
		if(scr->gscreen == nil)
			error("drawinit: no gscreen");
		if(scr->dev && scr->dev->drawinit)
			scr->dev->drawinit(scr);
		hwblank = scr->blank != nil;
		hwaccel = scr->fill != nil || scr->scroll != nil;
		vgascreenwin(scr);
		resetscreenimage();
		cursoron();
		return;

	case CMlinear:
		if(cb->nf!=2 && cb->nf!=3)
			error(Ebadarg);
		size = strtoul(cb->f[1], 0, 0);
		if(cb->nf == 2)
			align = 0;
		else
			align = strtoul(cb->f[2], 0, 0);
		if(screenaperture(size, align) < 0)
			error("not enough free address space");
		return;

	case CMpanning:
		if(strcmp(cb->f[1], "on") == 0){
			if(scr == nil || scr->cur == nil)
				error("set screen first");
			if(!scr->cur->doespanning)
				error("panning not supported");
			scr->gscreen->clipr = scr->gscreen->r;
			panning = 1;
		}
		else if(strcmp(cb->f[1], "off") == 0){
			scr->gscreen->clipr = physgscreenr;
			panning = 0;
		}else
			break;
		return;

	case CMhwaccel:
		if(strcmp(cb->f[1], "on") == 0)
			hwaccel = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			hwaccel = 0;
		else
			break;
		return;

	case CMhwblank:
		if(strcmp(cb->f[1], "on") == 0)
			hwblank = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			hwblank = 0;
		else
			break;
		return;
	}

	cmderror(cb, "bad VGA control message");
}

char Enooverlay[] = "No overlay support";

static long
vgawrite(Chan* c, void* a, long n, int64_t off)
{
	uint32_t offset = off;
	Cmdbuf *cb;
	VGAscr *scr;

	switch((uint32_t)c->qid.path){

	case Qdir:
		error(Eperm);

	case Qvgactl:
		if(offset || n >= READSTR)
			error(Ebadarg);
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		vgactl(cb);
		poperror();
		free(cb);
		return n;

	case Qvgaovl:
		scr = &vgascreen[0];
		if (scr->dev == nil || scr->dev->ovlwrite == nil) {
			error(Enooverlay);
			break;
		}
		return scr->dev->ovlwrite(scr, a, n, off);

	case Qvgaovlctl:
		scr = &vgascreen[0];
		if (scr->dev == nil || scr->dev->ovlctl == nil) {
			error(Enooverlay);
			break;
		}
		scr->dev->ovlctl(scr, c, a, n);
		return n;

	default:
		error(Egreg);
		break;
	}

	return 0;
}

Dev vgadevtab = {
	'v',
	"vga",

	vgareset,
	devinit,
	devshutdown,
	vgaattach,
	vgawalk,
	vgastat,
	vgaopen,
	devcreate,
	vgaclose,
	vgaread,
	devbread,
	vgawrite,
	devbwrite,
	devremove,
	devwstat,
};
