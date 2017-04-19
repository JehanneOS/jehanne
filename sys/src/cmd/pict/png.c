#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include "imagefile.h"

extern int	debug;
int	cflag = 0;
int	dflag = 0;
int	eflag = 0;
int	nineflag = 0;
int	threeflag = 0;
int	output = 0;
uint32_t	outchan = CMAP8;
Image	*image;
int	defaultcolor = 1;

enum{
	Border	= 2,
	Edge	= 5
};

char	*show(int, char*, int);

Rectangle
imager(Image *i)
{
	Point p1, p2;

	p1 = addpt(divpt(subpt(i->r.max, i->r.min), 2), i->r.min);
	p2 = addpt(divpt(subpt(screen->clipr.max, screen->clipr.min), 2), screen->clipr.min);
	return rectaddpt(i->r, subpt(p2, p1));
}

void
eresized(int new)
{
	Rectangle r;

	if(new && getwindow(display, Refnone) < 0){
		fprint(2, "png: can't reattach to window\n");
		exits("resize");
	}
	if(image == nil)
		return;
	r = imager(image);
	border(screen, r, -Border, nil, ZP);
	draw(screen, r, image, nil, image->r.min);
	flushimage(display, 1);
}

void
main(int argc, char *argv[])
{
	int fd, i;
	char *err;

	ARGBEGIN{
	case 'c':		/* produce encoded, compressed, bitmap file; no display by default */
		cflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	case 'D':
		debug++;
		break;
	case 'd':		/* suppress display of image */
		dflag++;
		break;
	case 'e':		/* disable floyd-steinberg error diffusion */
		eflag++;
		break;
	case 'k':		/* force black and white */
		defaultcolor = 0;
		outchan = GREY8;
		break;
	case '3':		/* produce encoded, compressed, three-color bitmap file; no display by default */
		threeflag++;
		/* fall through */
	case 't':		/* produce encoded, compressed, true-color bitmap file; no display by default */
		cflag++;
		dflag++;
		output++;
		defaultcolor = 0;
		outchan = RGB24;
		break;
	case 'v':		/* force RGBV */
		defaultcolor = 0;
		outchan = CMAP8;
		break;
	case '9':		/* produce plan 9, uncompressed, bitmap file; no display by default */
		nineflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	default:
		fprint(2, "usage: png [-39cdekrtv] [file.png ...]\n");
		exits("usage");
	}ARGEND;

	err = nil;
	if(argc == 0)
		err = show(0, "<stdin>", outchan);
	else{
		for(i=0; i<argc; i++){
			fd = open(argv[i], OREAD);
			if(fd < 0){
				fprint(2, "png: can't open %s: %r\n", argv[i]);
				err = "open";
			}else{
				err = show(fd, argv[i], outchan);
				close(fd);
			}
			if((nineflag || cflag) && argc>1 && err==nil){
				fprint(2, "png: exiting after one file\n");
				break;
			}
		}
	}
	exits(err);
}

char*
show(int fd, char *name, int outc)
{
	Rawimage **array, *r, *c;
	Image *i, *i2;
	int j, ch, outchan;
	long len;
	Biobuf b;
	char buf[32];
	static int inited;

	if(Binit(&b, fd, OREAD) < 0)
		return nil;
	outchan = outc;
	array = Breadpng(&b, CRGB);
	if(array == nil || array[0]==nil){
		fprint(2, "png: decode %s failed: %r\n", name);
		return "decode";
	}
	Bterm(&b);

	r = array[0];
	if(!dflag){
		if (!inited) {
			if(initdraw(0, 0, 0) < 0){
				fprint(2, "png: initdraw failed: %r\n");
				return "initdraw";
			}
			einit(Ekeyboard|Emouse);
			inited++;
		}
		if(defaultcolor && screen->depth>8 && outchan==CMAP8)
			outchan = RGB24;
	}
	if(outchan == CMAP8)
		c = torgbv(r, !eflag);
	else{
		switch(r->chandesc){
		case CY:
			outchan = GREY8;
			break;
		case CYA16:
			outchan = CHAN2(CGrey, 8, CAlpha, 8);
			break;
		case CRGB24:
			outchan = RGB24;
			break;
		case CRGBA32:
			outchan = RGBA32;
			break;
		}
		c = r;
	}
	if(c == nil){
		fprint(2, "png: conversion of %s failed: %r\n", name);
		return "torgbv";
	}
	if(!dflag){
		i = allocimage(display, c->r, outchan, 0, 0);
		if(i == nil){
			fprint(2, "png: allocimage %s failed: %r\n", name);
			return "allocimage";
		}
		if(loadimage(i, i->r, c->chans[0], c->chanlen) < 0){
			fprint(2, "png: loadimage %s of %d bytes failed: %r\n",
				name, c->chanlen);
			return "loadimage";
		}
		i2 = allocimage(display, c->r, outchan, 0, 0);
		draw(i2, i2->r, display->black, nil, ZP);
		draw(i2, i2->r, i, nil, i->r.min);
		image = i2;
		eresized(0);
		if((ch=ekbd())=='q' || ch==Kdel || ch==Keof)
			exits(nil);
		draw(screen, screen->clipr, display->white, nil, ZP);
		image = nil;
		freeimage(i);
	}
	if(nineflag){
		chantostr(buf, outchan);
		len = (c->r.max.x - c->r.min.x) * (c->r.max.y - c->r.min.y);
		switch(c->chandesc){
		case CY:
			// len *= 1;
			break;
		case CYA16:
			len *= 2;
			break;
		case CRGB24:
			len *= 3;
			break;
		case CRGBA32:
			len *= 4;
			break;
		}
		if(c->chanlen != len)
			fprint(2, "%s: writing %d bytes for len %ld chan %s\n",
				argv0, c->chanlen, len, buf);
		print("%11s %11d %11d %11d %11d ", buf,
			c->r.min.x, c->r.min.y, c->r.max.x, c->r.max.y);
		if(write(1, c->chans[0], c->chanlen) != c->chanlen){
			fprint(2, "png: %s: write error %r\n", name);
			return "write";
		}
	}else if(cflag){
		if(writerawimage(1, c) < 0){
			fprint(2, "png: %s: write error: %r\n", name);
			return "write";
		}
	}
	for(j=0; j<r->nchans; j++)
		free(r->chans[j]);
	free(r->cmap);
	free(r);
	free(array);
	if(c && c != r){
		free(c->chans[0]);
		free(c);
	}
	return nil;
}
