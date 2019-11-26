#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <event.h>
#include <cursor.h>

#include "imagefile.h"

typedef struct Icon Icon;
struct Icon
{
	Icon	*next;

	unsigned char	w;		/* icon width */
	unsigned char	h;		/* icon height */
	unsigned short	ncolor;		/* number of colors */
	unsigned short	nplane;		/* number of bit planes */
	unsigned short	bits;		/* bits per pixel */
	uint32_t	len;		/* length of data */
	uint32_t	offset;		/* file offset to data */

	Memimage	*img;
	Memimage	*mask;

	Rectangle r;		/* relative */
	Rectangle sr;		/* abs */
};

typedef struct Header Header;
struct Header
{
	uint	n;
	Icon	*first;
	Icon	*last;
};

int debug;
int cflag;
Mouse mouse;
Header h;
Image *background;

unsigned short
gets(unsigned char *p)
{
	return p[0] | (p[1]<<8);
}

uint32_t
getl(unsigned char *p)
{
	return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}

int
Bgetheader(Biobuf *b, Header *h)
{
	unsigned char buf[40];
	Icon *icon;
	int i;

	memset(h, 0, sizeof(*h));
	if(Bread(b, buf, 6) != 6)
		goto eof;
	if(gets(&buf[0]) != 0)
		goto header;
	if(gets(&buf[2]) != 1)
		goto header;
	h->n = gets(&buf[4]);
	for(i = 0; i < h->n; i++){
		icon = mallocz(sizeof(*icon), 1);
		if(icon == nil)
			sysfatal("malloc: %r");
		if(Bread(b, buf, 16) != 16)
			goto eof;
		icon->w = buf[0] == 0 ? 256 : buf[0];
		icon->h = buf[1] == 0 ? 256 : buf[1];
		icon->ncolor = buf[2] == 0 ? 256 : buf[2];
		icon->nplane = gets(&buf[4]);
		icon->bits = gets(&buf[6]);
		icon->len = getl(&buf[8]);
		icon->offset = getl(&buf[12]);
		if(i == 0)
			h->first = icon;
		else
			h->last->next = icon;
		h->last = icon;
	}
	return 0;

eof:
	werrstr("unexpected EOF");
	return -1;
header:
	werrstr("unknown header format");
	return -1;
}

unsigned char*
transcmap(Icon *icon, int ncolor, unsigned char *map)
{
	unsigned char *m, *p;
	int i;

	p = m = mallocz(sizeof(int)*(1<<icon->bits), 1);
	if(m == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < ncolor; i++){
		*p++ = rgb2cmap(map[2], map[1], map[0]);
		map += 4;
	}
	return m;
}

Memimage*
xor2img(Icon *icon, long chan, unsigned char *xor, unsigned char *map)
{
	unsigned char *data;
	Memimage *img;
	int inxlen;
	unsigned char *from, *to;
	int s, byte, mask;
	int x, y;

	inxlen = 4*((icon->bits*icon->w+31)/32);
	img = allocmemimage(Rect(0,0,icon->w,icon->h), chan);
	if(img == nil)
		return nil;

	if(chan != CMAP8){
		from = xor + icon->h*inxlen;
		for(y = 0; y < icon->h; y++){
			from -= inxlen;
			loadmemimage(img, Rect(0,y,icon->w,y+1), from, inxlen);
		}
		return img;
	}

	to = data = malloc(icon->w*icon->h);
	if(data == nil){
		freememimage(img);
		return nil;
	}

	/* rotate around the y axis, go to 8 bits, and convert color */
	mask = (1<<icon->bits)-1;
	for(y = 0; y < icon->h; y++){
		s = -1;
		byte = 0;
		from = xor + (icon->h - 1 - y)*inxlen;
		for(x = 0; x < icon->w; x++){
			if(s < 0){
				byte = *from++;
				s = 8-icon->bits;
			}
			*to++ = map[(byte>>s) & mask];
			s -= icon->bits;
		}
	}
	/* stick in an image */
	loadmemimage(img, Rect(0,0,icon->w,icon->h), data, icon->h*icon->w);
	free(data);
	return img;
}

Memimage*
and2img(Icon *icon, unsigned char *and)
{
	unsigned char *data;
	Memimage *img;
	int inxlen;
	int outxlen;
	unsigned char *from, *to;
	int x, y;

	inxlen = 4*((icon->w+31)/32);
	to = data = malloc(inxlen*icon->h);
	if(data == nil)
		return nil;

	/* rotate around the y axis and invert bits */
	outxlen = (icon->w+7)/8;
	for(y = 0; y < icon->h; y++){
		from = and + (icon->h - 1 - y)*inxlen;
		for(x = 0; x < outxlen; x++)
			*to++ = ~(*from++);
	}

	/* stick in an image */
	if(img = allocmemimage(Rect(0,0,icon->w,icon->h), GREY1))
		loadmemimage(img, Rect(0,0,icon->w,icon->h), data, icon->h*outxlen);

	free(data);
	return img;
}

int
Bgeticon(Biobuf *b, Icon *icon)
{
	unsigned char *end;
	unsigned char *xor;
	unsigned char *and;
	unsigned char *cm;
	unsigned char *buf;
	unsigned char *map2map;
	Memimage *img;
	unsigned char magic[4];
	int ncolor;
	long chan;

	Bseek(b, icon->offset, 0);
	if(Bread(b, magic, 4) != 4){
		werrstr("unexpected EOF");
		return -1;
	}
	if(magic[0] == 137 && memcmp(magic+1, "PNG", 3) == 0){
		Rawimage **png;

		Bseek(b, -4, 1);
		png = Breadpng(b, CRGB);
		if(png == nil || png[0] == nil)
			return -1;
		switch(png[0]->chandesc){
		case CY:
			chan = GREY8;
			break;
		case CYA16:
			chan = CHAN2(CGrey, 8, CAlpha, 8);
			break;
		case CRGB24:
			chan = RGB24;
			break;
		case CRGBA32:
			chan = RGBA32;
			break;
		default:
			werrstr("bad icon png channel descriptor");
			return -1;
		}
		icon->mask = nil;
		icon->img = allocmemimage(png[0]->r, chan);
		loadmemimage(icon->img, icon->img->r, png[0]->chans[0], png[0]->chanlen);
		return 0;
	}

	if(getl(magic) != 40){
		werrstr("bad icon bmp header");
		return -1;
	}
	if(icon->len < 40){
		werrstr("bad icon bmp header length");
		return -1;
	}
	buf = malloc(icon->len);
	if(buf == nil)
		return -1;
	memmove(buf, magic, 4);
	if(Bread(b, buf+4, icon->len-4) != icon->len-4){
		werrstr("unexpected EOF");
		return -1;
	}

	/* this header's info takes precedence over previous one */
	ncolor = 0;
	icon->w = getl(buf+4);
	icon->h = getl(buf+8)>>1;
	icon->nplane = gets(buf+12);
	icon->bits = gets(buf+14);

	/* limit what we handle */
	switch(icon->bits){
	case 1:
	case 2:
	case 4:
	case 8:
		ncolor = icon->ncolor;
		if(ncolor > (1<<icon->bits))
			ncolor = 1<<icon->bits;
		chan = CMAP8;
		break;
	case 15:
	case 16:
		chan = RGB16;
		break;
	case 24:
		chan = RGB24;
		break;
	case 32:
		chan = ARGB32;
		break;
	default:
		werrstr("don't support %d bit pixels", icon->bits);
		return -1;
	}
	if(icon->nplane != 1){
		werrstr("don't support %d planes", icon->nplane);
		return -1;
	}

	xor = cm = buf + 40;
	if(chan == CMAP8)
		xor += 4*ncolor;
	end = xor + icon->h*4*((icon->bits*icon->w+31)/32);
	if(end < buf || end > buf+icon->len){
		werrstr("bad icon length %zux != %lux", end - buf, icon->len);
		return -1;
	}

	/* translate the color map to a plan 9 one */
	map2map = nil;
	if(chan == CMAP8)
		map2map = transcmap(icon, ncolor, cm);

	/* convert the images */
	icon->img = xor2img(icon, chan, xor, map2map);
	if(icon->img == nil){
		werrstr("xor2img: %r");
		return -1;
	}
	icon->mask = nil;

	/* check for and mask */
	and = end;
	end += icon->h*4*((icon->w+31)/32);
	if(end <= buf+icon->len)
		icon->mask = and2img(icon, and);

	/* so that we save an image with a white background */
	if(img = allocmemimage(icon->img->r, icon->img->chan)){
		memfillcolor(img, DWhite);
		memimagedraw(img, icon->img->r, icon->img, ZP, icon->mask, ZP, SoverD);
		freememimage(icon->img);
		icon->img = img;
	}

	free(buf);
	free(map2map);
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -c ] [ file ]\n", argv0);
	exits("usage");
}

enum
{
	Mimage,
	Mmask,
	Mexit,

	Up= 1,
	Down= 0,
};

char	*menu3str[] = {
	[Mimage]	"write image",
	[Mmask]		"write mask",
	[Mexit]		"exit",
	0,
};

Menu	menu3 = {
	menu3str
};

Cursor sight = {
	{-7, -7},
	{0x1F, 0xF8, 0x3F, 0xFC, 0x7F, 0xFE, 0xFB, 0xDF,
	 0xF3, 0xCF, 0xE3, 0xC7, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC7, 0xF3, 0xCF,
	 0x7B, 0xDF, 0x7F, 0xFE, 0x3F, 0xFC, 0x1F, 0xF8,},
	{0x00, 0x00, 0x0F, 0xF0, 0x31, 0x8C, 0x21, 0x84,
	 0x41, 0x82, 0x41, 0x82, 0x41, 0x82, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x41, 0x82, 0x41, 0x82, 0x41, 0x82,
	 0x21, 0x84, 0x31, 0x8C, 0x0F, 0xF0, 0x00, 0x00,}
};

void
buttons(int ud)
{
	while((mouse.buttons==0) != ud)
		mouse = emouse();
}

void
mesg(char *fmt, ...)
{
	va_list arg;
	char buf[1024];
	static char obuf[1024];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	string(screen, screen->r.min, background, ZP, font, obuf);
	string(screen, screen->r.min, display->white, ZP, font, buf);
	strcpy(obuf, buf);
}

void
doimage(Icon *icon)
{
	int rv;
	char file[256];
	int fd;

	rv = -1;
	snprint(file, sizeof(file), "%dx%d.img", icon->w, icon->h);
	fd = sys_create(file, OWRITE, 0664);
	if(fd >= 0){
		rv = writememimage(fd, icon->img);
		sys_close(fd);
	}
	if(rv < 0)
		mesg("error writing %s: %r", file);
	else
		mesg("created %s", file);
}

void
domask(Icon *icon)
{
	int rv;
	char file[64];
	int fd;

	if(icon->mask == nil)
		return;

	rv = -1;
	snprint(file, sizeof(file), "%dx%d.mask", icon->w, icon->h);
	fd = sys_create(file, OWRITE, 0664);
	if(fd >= 0){
		rv = writememimage(fd, icon->mask);
		sys_close(fd);
	}
	if(rv < 0)
		mesg("error writing %s: %r", file);
	else
		mesg("created %s", file);
}

void
apply(void (*f)(Icon*))
{
	Icon *icon;

	esetcursor(&sight);
	buttons(Down);
	if(mouse.buttons == 4)
		for(icon = h.first; icon; icon = icon->next)
			if(ptinrect(mouse.xy, icon->sr)){
				buttons(Up);
				f(icon);
				break;
			}
	buttons(Up);
	esetcursor(0);
}

void
menu(void)
{
	int sel;

	sel = emenuhit(3, &mouse, &menu3);
	switch(sel){
	case Mimage:
		apply(doimage);
		break;
	case Mmask:
		apply(domask);
		break;
	case Mexit:
		exits(0);
		break;
	}
}

void
mousemoved(void)
{
	Icon *icon;

	for(icon = h.first; icon; icon = icon->next)
		if(ptinrect(mouse.xy, icon->sr)){
			mesg("%dx%d", icon->w, icon->h);
			return;
		}
	mesg("");
}

enum
{
	BORDER= 1,
};

Image*
screenimage(Memimage *m)
{
	Rectangle r;
	Image *i;

	if(i = allocimage(display, m->r, m->chan, 0, DNofill)){
		r = m->r;
		while(r.min.y < m->r.max.y){
			r.max.y = r.min.y+1;
			loadimage(i, r, byteaddr(m, r.min), bytesperline(r, m->depth));
			r.min.y++;
		}
	}
	return i;
}

void
eresized(int new)
{
	Icon *icon;
	Image *i;
	Rectangle r;

	if(new && getwindow(display, Refnone) < 0)
		sysfatal("can't reattach to window");
	draw(screen, screen->clipr, background, nil, ZP);
	r.max.x = screen->r.min.x;
	r.min.y = screen->r.min.y + font->height + 2*BORDER;
	for(icon = h.first; icon != nil; icon = icon->next){
		r.min.x = r.max.x + BORDER;
		r.max.x = r.min.x + Dx(icon->img->r);
		r.max.y = r.min.y + Dy(icon->img->r);
		if(i = screenimage(icon->img)){
			draw(screen, r, i, nil, ZP);
			freeimage(i);
		}
		border(screen, r, -BORDER, display->black, ZP);
		icon->sr = r;
	}
	flushimage(display, 1);
}

void
main(int argc, char **argv)
{
	Biobuf in;
	Icon *icon;
	int num, fd;
	Rectangle r;
	Event e;

	ARGBEGIN{
	case 'd':
		debug = 1;
		break;
	case 'c':
		cflag = 1;
		break;
	default:
		usage();
	}ARGEND;

	fd = -1;
	switch(argc){
	case 0:
		fd = 0;
		break;
	case 1:
		fd = sys_open(argv[0], OREAD);
		if(fd < 0)
			sysfatal("opening: %r");
		break;
	default:
		usage();
		break;
	}

	memimageinit();
	Binit(&in, fd, OREAD);

	if(Bgetheader(&in, &h) < 0)
		sysfatal("reading header: %r");

	num = 0;
	r.min = Pt(4, 4);
	for(icon = h.first; icon != nil; icon = icon->next){
		if(Bgeticon(&in, icon) < 0){
			fprint(2, "%s: read fail: %r\n", argv0);
			continue;
		}
		if(debug)
			fprint(2, "w %ud h %ud ncolor %ud bits %ud len %lud offset %lud\n",
			   icon->w, icon->h, icon->ncolor, icon->bits, icon->len, icon->offset);
		r.max = addpt(r.min, Pt(icon->w, icon->h));
		icon->r = r;
		if(cflag){
			writememimage(1, icon->img);
			exits(0);
		}
		r.min.x += r.max.x;
		num++;
	}

	if(num == 0 || cflag)
		sysfatal("no images");

	initdraw(nil, nil, "ico");
	background = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0x808080FF);
	eresized(0);
	einit(Emouse|Ekeyboard);
	for(;;)
		switch(event(&e)){
		case Ekeyboard:
			break;
		case Emouse:
			mouse = e.mouse;
			if(mouse.buttons & 4)
				menu();
			else
				mousemoved();
			break;
		}
	/* not reached */
}
