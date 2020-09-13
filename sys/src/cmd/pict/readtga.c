/*
 * TGA is a fairly dead standard, however in the video industry
 * it is still used a little for test patterns and the like.
 *
 * Thus we ignore any alpha channels.
 */

#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <chartypes.h>
#include "imagefile.h"

enum {
	HdrLen = 18,
};

typedef struct {
	int idlen;			/* length of string after header */
	int cmaptype;		/* 1 => datatype = 1 => colourmapped */
	int datatype;		/* see below */
	int cmaporigin;		/* index of first entry in colour map */
	int cmaplen;		/* length of colour map */
	int cmapbpp;		/* bits per pixel of colour map: 16, 24, or 32 */
	int xorigin;		/* source image origin */
	int yorigin;
	int width;
	int height;
	int bpp;			/* bits per pixel of image: 16, 24, or 32 */
	int descriptor;
	unsigned char *cmap;		/* colour map (optional) */
} Tga;

/*
 * descriptor:
 * d0-3 = number of attribute bits per pixel
 * d4 	= reserved, always zero
 * d6-7	= origin: 0=lower left, 1=upper left, 2=lower right, 3=upper right
 * d8-9 = interleave: 0=progressive, 1=2 way, 3=4 way, 4=reserved.
 */

char *datatype[] = {
	[0]		"No image data",
	[1]		"Color-mapped",
	[2]		"RGB",
	[3]		"B&W",
	[9]		"RLE color-mapped",
	[10]	"RLE RGB",
	[11]	"RLE B&W",
	[32]	"Compressed color",
	[33]	"Quadtree compressed color",
};

static int
Bgeti(Biobuf *bp)
{
	int x, y;

	if((x = Bgetc(bp)) < 0)
		return -1;
	if((y = Bgetc(bp)) < 0)
		return -1;
	return (y<<8)|x;
}

static int
fixcmap(unsigned char *cmap, int *cmapbpp, int cmaplen)
{
	int i;
	unsigned short x;
	unsigned char tmp;

	switch(*cmapbpp){
	case 32:
		/* swap B with R */
		for(i = 0; i < cmaplen; i++){
			tmp = cmap[4*i+0];
			cmap[4*i+0] = cmap[4*i+2];
			cmap[4*i+2] = tmp;
		}
		break;
	case 24:
		/* swap B with R */
		for(i = 0; i < cmaplen; i++){
			tmp = cmap[3*i+0];
			cmap[3*i+0] = cmap[3*i+2];
			cmap[3*i+2] = tmp;
		}
		break;
	case 16:
	case 15:
		/* convert to 24-bit colormap */
		if((cmap = realloc(cmap, 3*cmaplen)) == nil)
			return -1;
		for(i = cmaplen-1; i >= 0; i--){
			x = (cmap[2*i+1]<<8) | cmap[2*i+0];
			tmp = (x>>0)&0x1f;
			cmap[3*i+2] = (tmp<<3) | (tmp>>2);
			tmp = (x>>5)&0x1f;
			cmap[3*i+1] = (tmp<<3) | (tmp>>2);
			tmp = (x>>10)&0x1f;
			cmap[3*i+0] = (tmp<<3) | (tmp>>2);
		}
		*cmapbpp = 24;
		break;
	default:
		break;
	}

	return 0;
}

static Tga *
rdhdr(Biobuf *bp)
{
	int n;
	Tga *h;

	if((h = malloc(sizeof(Tga))) == nil)
		return nil;
	if((h->idlen = Bgetc(bp)) == -1)
		return nil;
	if((h->cmaptype = Bgetc(bp)) == -1)
		return nil;
	if((h->datatype = Bgetc(bp)) == -1)
		return nil;
	if((h->cmaporigin = Bgeti(bp)) == -1)
		return nil;
	if((h->cmaplen = Bgeti(bp)) == -1)
		return nil;
	if((h->cmapbpp = Bgetc(bp)) == -1)
		return nil;
	if((h->xorigin = Bgeti(bp)) == -1)
		return nil;
	if((h->yorigin = Bgeti(bp)) == -1)
		return nil;
	if((h->width = Bgeti(bp)) == -1)
		return nil;
	if((h->height = Bgeti(bp)) == -1)
		return nil;
	if((h->bpp = Bgetc(bp)) == -1)
		return nil;
	if((h->descriptor = Bgetc(bp)) == -1)
		return nil;

	/* skip over ID, usually empty anyway */
	if(Bseek(bp, h->idlen, 1) < 0){
		free(h);
		return nil;
	}

	if(h->cmaptype == 0){
		h->cmap = 0;
		return h;
	}

	/* skip over unused color map data */
	n = (h->cmapbpp/8)*h->cmaporigin;
	if(Bseek(bp, n, 1) < 0){
		free(h);
		return nil;
	}
	h->cmaplen -= h->cmaporigin;

	n = (h->cmapbpp/8)*h->cmaplen;
	if((h->cmap = malloc(n)) == nil){
		free(h);
		return nil;
	}
	if(Bread(bp, h->cmap, n) != n){
		free(h);
		free(h->cmap);
		return nil;
	}
	if(fixcmap(h->cmap, &h->cmapbpp, h->cmaplen) != 0){
		free(h);
		free(h->cmap);
		return nil;
	}
	return h;
}

static int
cmap(Biobuf *bp, unsigned char *l, int num)
{
	return Bread(bp, l, num);
}

static int
luma(Biobuf *bp, int bpp, unsigned char *l, int num)
{
	char tmp[2];
	int got;

	if(bpp == 8){
		got = Bread(bp, l, num);
	}
	else{
		for(got = 0; got < num; got++){
			if(Bread(bp, tmp, 2) != 2)
				break;
			*l++ = tmp[0];
		}
	}
	return got;
}

static int
luma_rle(Biobuf *bp, int bpp, unsigned char *l, int num)
{
	unsigned char len, p;
	int got;

	for(got = 0; got < num;){
		if(Bread(bp, &len, 1) != 1)
			break;
		if(len & 0x80){
			len &= 0x7f;
			if(luma(bp, bpp, &p, 1) != 1)
				break;
			for(len++; len > 0 && got < num; len--, got++)
				*l++ = p;
		}
		else{
			for(len++; len > 0 && got < num; len--, got++)
				if(luma(bp, bpp, l++, 1) != 1)
					return got;
		}
	}
	return got;
}

static int
cmap_rle(Biobuf *bp, unsigned char *l, int num)
{
	return luma_rle(bp, 8, l, num);
}

static int
rgba(Biobuf *bp, int bpp, unsigned char *r, unsigned char *g, unsigned char *b, int num)
{
	int i;
	unsigned char buf[4], tmp;
	unsigned short x;

	switch(bpp){
	case 16:
	case 15:
		for(i = 0; i < num; i++){
			if(Bread(bp, buf, 2) != 2)
				break;
			x = (buf[1]<<8) | buf[0];
			tmp = (x>>0)&0x1f;
			*b++ = (tmp<<3) | (tmp>>2);
			tmp = (x>>5)&0x1f;
			*g++ = (tmp<<3) | (tmp>>2);
			tmp = (x>>10)&0x1f;
			*r++ = (tmp<<3) | (tmp>>2);
		}
		break;
	case 24:
		for(i = 0; i < num; i++){
			if(Bread(bp, buf, 3) != 3)
				break;
			*b++ = buf[0];
			*g++ = buf[1];
			*r++ = buf[2];
		}
		break;
	case 32:
		for(i = 0; i < num; i++){
			if(Bread(bp, buf, 4) != 4)
				break;
			*b++ = buf[0];
			*g++ = buf[1];
			*r++ = buf[2];
		}
		break;
	default:
		i = 0;
		break;
	}
	return i;
}

static int
rgba_rle(Biobuf *bp, int bpp, unsigned char *r, unsigned char *g, unsigned char *b, int num)
{
	unsigned char len;
	int i, got;

	for(got = 0; got < num; got += len){
		if(Bread(bp, &len, 1) != 1)
			break;
		if(len & 0x80){
			len &= 0x7f;
			len += 1;	/* run of zero is meaningless */
			if(rgba(bp, bpp, r, g, b, 1) != 1)
				break;
			for(i = 1; i < len && got+i < num; i++){
				r[i] = *r;
				g[i] = *g;
				b[i] = *b;
			}
			len = i;
		}
		else{
			len += 1;	/* raw block of zero is meaningless */
			if(rgba(bp, bpp, r, g, b, len) != len)
				break;
		}
		r += len;
		g += len;
		b += len;
	}
	return got;
}

int
flip(Rawimage *ar)
{
	int w, h, c, l;
	unsigned char *t, *s, *d;

	w = Dx(ar->r);
	h = Dy(ar->r);
	if((t = malloc(w)) == nil){
		werrstr("ReadTGA: no memory - %r\n");
		return -1;
	}

	for(c = 0; c < ar->nchans; c++){
		s = ar->chans[c];
		d = ar->chans[c] + ar->chanlen - w;
		for(l = 0; l < (h/2); l++){
			memcpy(t, s, w);
			memcpy(s, d, w);
			memcpy(d, t, w);
			s += w;
			d -= w;
		}
	}
	free(t);
	return 0;
}

int
reflect(Rawimage *ar)
{
	int w, h, c, l, p;
	unsigned char t, *sol, *eol, *s, *d;

	w = Dx(ar->r);
	h = Dy(ar->r);

	for(c = 0; c < ar->nchans; c++){
		sol = ar->chans[c];
		eol = ar->chans[c] +w -1;
		for(l = 0; l < h; l++){
			s = sol;
			d = eol;
			for(p = 0; p < w/2; p++){
				t = *s;
				*s = *d;
				*d = t;
				s++;
				d--;
			}
			sol += w;
			eol += w;
		}
	}
	return 0;
}


Rawimage**
Breadtga(Biobuf *bp)
{
	Tga *h;
	int n, c, num;
	unsigned char *r, *g, *b;
	Rawimage *ar, **array;

	if((h = rdhdr(bp)) == nil){
		werrstr("ReadTGA: bad header %r");
		return nil;
	}

	if(0){
		fprint(2, "idlen=%d\n", h->idlen);
		fprint(2, "cmaptype=%d\n", h->cmaptype);
		fprint(2, "datatype=%s\n", datatype[h->datatype]);
		fprint(2, "cmaporigin=%d\n", h->cmaporigin);
		fprint(2, "cmaplen=%d\n", h->cmaplen);
		fprint(2, "cmapbpp=%d\n", h->cmapbpp);
		fprint(2, "xorigin=%d\n", h->xorigin);
		fprint(2, "yorigin=%d\n", h->yorigin);
		fprint(2, "width=%d\n", h->width);
		fprint(2, "height=%d\n", h->height);
		fprint(2, "bpp=%d\n", h->bpp);
		fprint(2, "descriptor=%d\n", h->descriptor);
	}

	array = nil;
	if((ar = calloc(1, sizeof(Rawimage))) == nil){
		werrstr("ReadTGA: no memory - %r\n");
		goto Error;
	}

	if((array = calloc(2, sizeof(Rawimage *))) == nil){
		werrstr("ReadTGA: no memory - %r\n");
		goto Error;
	}
	array[0] = ar;
	array[1] = nil;

	if(h->datatype == 3 || h->datatype == 11){
		ar->nchans = 1;
		ar->chandesc = CY;
	}
	else if(h->datatype == 1){
		ar->nchans = 1;
		ar->chandesc = CRGB1;
	}
	else if(h->datatype == 9){
		ar->nchans = 1;
		ar->chandesc = (h->cmapbpp == 32) ? CRGBV : CRGB1;
	}
	else{
		ar->nchans = 3;
		ar->chandesc = CRGB;
	}

	ar->cmap = h->cmap;
	ar->cmaplen = (h->cmapbpp/8)*h->cmaplen;
	ar->chanlen = h->width*h->height;
	ar->r = Rect(0, 0, h->width, h->height);
	for(c = 0; c < ar->nchans; c++)
		if((ar->chans[c] = malloc(h->width*h->height)) == nil){
			werrstr("ReadTGA: no memory - %r\n");
			goto Error;
		}
	r = ar->chans[0];
	g = ar->chans[1];
	b = ar->chans[2];

	num = h->width*h->height;
	switch(h->datatype){
	case 1:
		n = cmap(bp, r, num);
		break;
	case 2:
		n = rgba(bp, h->bpp, r, g, b, num);
		break;
	case 3:
		n = luma(bp, h->bpp, r, num);
		break;
	case 9:
		n = cmap_rle(bp, r, num);
		break;
	case 10:
		n = rgba_rle(bp, h->bpp, r, g, b, num);
		break;
	case 11:
		n = luma_rle(bp, h->bpp, r, num);
		break;
	default:
		werrstr("ReadTGA: type=%d (%s) unsupported\n", h->datatype, datatype[h->datatype]);
		goto Error;
 	}

	if(n != num){
		werrstr("ReadTGA: decode fail (%d!=%d) - %r\n", n, num);
		goto Error;
	}
	if((h->descriptor&(1<<4)) != 0)
		reflect(ar);
	if((h->descriptor&(1<<5)) == 0)
		flip(ar);

	free(h);
	return array;
Error:

	if(ar)
		for (c = 0; c < ar->nchans; c++)
			free(ar->chans[c]);
	free(ar);
	free(array);
	free(h->cmap);
	free(h);
	return nil;
}

Rawimage**
readtga(int fd)
{
	Rawimage * *a;
	Biobuf b;

	if(Binit(&b, fd, OREAD) < 0)
		return nil;
	a = Breadtga(&b);
	Bterm(&b);
	return a;
}


