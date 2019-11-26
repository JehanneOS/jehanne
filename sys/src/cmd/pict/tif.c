#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include "imagefile.h"

int cflag = 0;
int dflag = 0;
int eflag = 0;
int nineflag = 0;
int threeflag = 0;
int output = 0;
Image *image;
int defaultcolor = 1;

enum {
	Border = 2,
	Edge = 5
};

int init(void);
char *show(int, char *, int);

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

	if(new && getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r");
	if(image == nil)
		return;
	r = imager(image);
	border(screen, r, -Border, nil, ZP);
	drawop(screen, r, image, nil, image->r.min, S);
	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: %s [-39cdektv] [file.tif ...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int fd, i;
	char *err;
	uint32_t outchan;

	outchan = CMAP8;
	ARGBEGIN {
	/*
	* produce encoded, compressed, bitmap file;
	* no display by default
	*/
	case 'c':
		cflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	/* suppress display of image */
	case 'd':
		dflag++;
		break;
	/* disable floyd-steinberg error diffusion */
	case 'e':
		eflag++;
		break;
	/* force black and white */
	case 'k':
		defaultcolor = 0;
		outchan = GREY8;
		break;
	/*
	* produce encoded, compressed, three-color
	* bitmap file; no display by default
	*/
	case '3':
		threeflag++;
		/* fall through */
	/*
	* produce encoded, compressed, true-color
	* bitmap file; no display by default
	*/
	case 't':
		cflag++;
		dflag++;
		output++;
		defaultcolor = 0;
		outchan = RGB24;
		break;
	/* force RGBV */
	case 'v':
		defaultcolor = 0;
		outchan = CMAP8;
		break;
	/*
	* produce plan 9, uncompressed, bitmap file;
	* no display by default
	*/
	case '9':
		nineflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	default:
		usage();
	} ARGEND

	if(argc <= 0)
		exits(show(0, "<stdin>", outchan));
	err = nil;
	for(i = 0; i < argc; i++) {
		if((fd = sys_open(argv[i], OREAD)) < 0) {
			fprint(2, "%s: open %s: %r\n",
				argv0, argv[i]);
			err = "open";
		} else {
			err = show(fd, argv[i], outchan);
			sys_close(fd);
		}
		if((nineflag || cflag) && argc > 1 && err == nil) {
			fprint(2, "%s: exiting after one file\n",
				argv0);
			break;
		}
	}
	exits(err);
}

int
init(void)
{
	static int inited = 0;

	if(!inited) {
		if(initdraw(0, 0, 0) < 0) {
			fprint(2, "%s: initdraw: %r", argv0);
			return -1;
		}
		einit(Ekeyboard|Emouse);
		inited++;
	}
	return 0;
}

char *
show(int fd, char *name, int outchan)
{
	Rawimage **array, *r, *c;
	Image *i;
	int j, ch;
	Biobuf b;
	char buf[32];

	if(Binit(&b, fd, OREAD) < 0)
		return nil;
	array = Breadtif(&b, CRGB24);
	if(array == nil || array[0] == nil) {
		if(array != nil)
			free(array);
		fprint(2, "%s: decode %s failed: %r\n",
			argv0, name);
		return "decode";
	}
	Bterm(&b);
	if(!dflag) {
		if(init() < 0)
			return "initdraw";
		if(defaultcolor && screen->depth > 8)
			outchan = RGB24;
	}
	r = array[0];
	if(outchan != CMAP8) {
		switch(r->chandesc) {
		case CY:
			outchan = GREY8;
			break;
		case CRGB24:
			outchan = RGB24;
			break;
		}
		c = r;
	} else if((c = torgbv(r, !eflag)) == nil) {
		fprint(2, "%s: conversion of %s failed: %r\n",
			argv0, name);
		return "torgbv";
	}
	if(!dflag) {
		i = allocimage(display, c->r, outchan, 0, 0);
		if(i == nil) {
			fprint(2, "%s: allocimage %s: %r\n",
				argv0, name);
			return "allocimage";
		}
		if(loadimage(i, i->r, c->chans[0],
			c->chanlen) < 0) {
			fprint(2, "%s: loadimage %s: %r\n",
				argv0, name);
			return "loadimage";
		}
		image = i;
		eresized(0);
		ch = ekbd();
		if(ch == 'q' || ch == Kdel || ch == Keof)
			exits(nil);
		draw(screen, screen->clipr, display->white,
			nil, ZP);
		image = nil;
		freeimage(i);
	}
	if(nineflag) {
		chantostr(buf, outchan);
		print("%11s %11d %11d %11d %11d ", buf,
			c->r.min.x, c->r.min.y,
			c->r.max.x, c->r.max.y);
		if(jehanne_write(1, c->chans[0], c->chanlen) !=
			c->chanlen) {
			fprint(2, "%s: %s: write error: %r\n",
				argv0, name);
			return "write";
		}
	} else if(cflag && writerawimage(1, c) < 0) {
		fprint(2, "%s: %s: write error: %r\n",
			argv0, name);
		return "write";
	}
	if(c != nil && c != r) {
		free(c->chans[0]);
		free(c);
	}
	for(j = 0; j < r->nchans; j++)
		free(r->chans[j]);
	free(r);
	free(array);
	return nil;
}
