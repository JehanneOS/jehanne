/*
* code/documentation from:
* http://www.w3.org/Graphics/JPEG/jfif3.pdf
* http://www.w3.org/Graphics/JPEG/itu-t81.pdf
* http://en.wikipedia.org/wiki/JPEG
* http://en.wikibooks.org/wiki/JPEG_-_Idea_and_Practice
* http://code.google.com/p/go/source/browse/src/pkg/image/jpeg/writer.go
* /sys/src/cmd/jpg
*
* fdct code from:
* http://www.ijg.org/files/jpegsrc.v8c.tar.gz
* http://code.google.com/p/go/source/browse/src/pkg/image/jpeg/fdct.go
*/
#include <u.h>
#include <lib9.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>
#include "imagefile.h"

/*
* imported from libdraw/arith.c to permit
* extern log2 function
*/
static int log2[] = {
	-1, 0, 1, -1, 2, -1, -1, -1, 3,
	-1, -1, -1, -1, -1, -1, -1, 4,
	-1, -1, -1, -1, -1, -1, -1, 4 /* BUG */,
	-1, -1, -1, -1, -1, -1, -1, 5
};

/* fdct constants */
enum {
	Fix02 = 2446, /* 0.298631336 */
	Fix03 = 3196, /* 0.390180644 */
	Fix05 = 4433, /* 0.541196100 */
	Fix07 = 6270, /* 0.765366865 */
	Fix08 = 7373, /* 0.899976223 */
	Fix11 = 9633, /* 1.175875602 */
	Fix15 = 12299, /* 1.501321110 */
	Fix18 = 15137, /* 1.847759065 */
	Fix19 = 16069, /* 1.961570560 */
	Fix20 = 16819, /* 2.053119869 */
	Fix25 = 20995, /* 2.562915447 */
	Fix30 = 25172 /* 3.072711026 */
};

static int zigzag[64] = {
	0, 1, 5, 6, 14, 15, 27, 28,
	2, 4, 7, 13, 16, 26, 29, 42,
	3, 8, 12, 17, 25, 30, 41, 43,
	9, 11, 18, 24, 31, 40, 44, 53,
	10, 19, 23, 32, 39, 45, 52, 54,
	20, 22, 33, 38, 46, 51, 55, 60,
	21, 34, 37, 47, 50, 56, 59, 61,
	35, 36, 48, 49, 57, 58, 62, 63
};

static int invzigzag[64] = {
	0, 1, 8, 16, 9, 2, 3, 10,
	17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

/* section K.1 for quantization tables */
static int qt[2][64] = {
	/* luminance */
	{16, 11, 10, 16, 24, 40, 51, 61,
	12, 12, 14, 19, 26, 58, 60, 55,
	14, 13, 16, 24, 40, 57, 69, 56,
	14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77,
	24, 35, 55, 64, 81, 104, 113, 92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103, 99},

	/* chrominance */
	{17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99}
};

/* section K.3.3 for huffman tables */
static int dcbits[2][16] = {
	/* luminance */
	{0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

	/* chrominance */
	{0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static int dchuffval[2][12] = {
	/* luminance */
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b},

	/* chrominance */
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b}
};

static int acbits[2][16] = {
	/* luminance */
	{0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d},

	/* chrominance */
	{0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77}
};

static int achuffval[2][162] = {
	/* luminance */
	{0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
	0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
	0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
	0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
	0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
	0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4a, 0x53,
	0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
	0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
	0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
	0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
	0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
	0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
	0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
	0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
	0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa},

	/* chrominance */
	{0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
	0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
	0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
	0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
	0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
	0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
	0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
	0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
	0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
	0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
	0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
	0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
	0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
	0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}
};

static int ehufcod[2][12]; /* dc codes */
static int ehufsid[2][12]; /* dc sizes */
static int ehufcoa[2][251]; /* ac codes */
static int ehufsia[2][251]; /* ac sizes */

static int byte;
static int nbyte;

/* utilities */
static int Bputs(Biobuf *, int);
static int min(int, int);

/* encoding */
static void grey2rgb(int *, int *, int *, int, int);
static void rgb2ycc(int *, int *, int *, int, int, int);
static void fdct(int *, int);
static int csize(int, int);
static void writebyte(Biobuf *);
static void writebits(Biobuf *, int, int);
static void writecode(Biobuf *, int, int);
static int huf(Biobuf *, int *, int, int, int);
static char *toycc1(int *, int *, int *, int, int, int, int, int,
	unsigned char *, int, int);
static char *toycc2(int *, int *, int *, int, int, int, int, int,
	unsigned char *, int, int);
static char *encode(Biobuf *, Rectangle, unsigned char *, uint32_t, int,
	int, int);

/* huffman tables */
static void makehuf(int *, int *, int *, int *, int);

/* tables, markers, headers, trailers */
static void writejfif(Biobuf *, int, int);
static void writecomment(Biobuf *, char *);
static void writequant(Biobuf *, int, int);
static void writehuffman(Biobuf *, int, int);
static void writeframe(Biobuf *, int, int, int);
static void writescan(Biobuf *, int);
static void writeheader(Biobuf *, int, int, char *, int, int);
static void writetrailer(Biobuf *);
static char *writedata(Biobuf *, Image *, Memimage *, int, int);
static char *writejpg0(Biobuf *, Image *, Memimage *,
	Rectangle, uint32_t, char *, int, int);

static int
Bputs(Biobuf *b, int s)
{
	if(Bputc(b, s>>8) < 0)
		return -1;
	return Bputc(b, s);
}

static int
min(int a, int b)
{
	return a < b? a: b;
}

static void
grey2rgb(int *r, int *g, int *b, int c, int depth)
{
	if(depth == 1) {
		if(c != 0)
			c = 0xff;
	} else if(depth == 2)
		c = (c << 6) | (c << 4) | (c << 2) | c;
	else
		c = (c << 4) | c;
	c = cmap2rgb(c);
	*r = (c >> 16) & 0xff;
	*g = (c >> 8) & 0xff;
	*b = c & 0xff;
}

static void
rgb2ycc(int *y, int *cb, int *cr, int r, int g, int b)
{
	*y = (int)(0.299*r + 0.587*g + 0.114*b);
	*cb = (int)(128.0 - 0.1687*r - 0.3313*g + 0.5*b);
	*cr = (int)(128.0 + 0.5*r - 0.4187*g - 0.0813*b);
}

/* coefficients remain scaled up by 8 at the end */
static void
fdct(int *b, int sflag)
{
	int x, y, z, tmp0, tmp1, tmp2, tmp3;
	int tmp10, tmp12, tmp11, tmp13;

	/* rows */
	for(y = 0; y < 8; y++) {
		tmp0 = b[y*8+0] + b[y*8+7];
		tmp1 = b[y*8+1] + b[y*8+6];
		tmp2 = b[y*8+2] + b[y*8+5];
		tmp3 = b[y*8+3] + b[y*8+4];

		tmp10 = tmp0 + tmp3;
		tmp12 = tmp0 - tmp3;
		tmp11 = tmp1 + tmp2;
		tmp13 = tmp1 - tmp2;

		tmp0 = b[y*8+0] - b[y*8+7];
		tmp1 = b[y*8+1] - b[y*8+6];
		tmp2 = b[y*8+2] - b[y*8+5];
		tmp3 = b[y*8+3] - b[y*8+4];

		b[y*8+0] = (tmp10 + tmp11 - 8*128) << 2;
		b[y*8+4] = (tmp10 - tmp11) << 2;

		z = (tmp12 + tmp13) * Fix05;
		z += 1 << 10;
		b[y*8+2] = (z + tmp12*Fix07) >> 11;
		b[y*8+6] = (z - tmp13*Fix18) >> 11;

		tmp10 = tmp0 + tmp3;
		tmp11 = tmp1 + tmp2;
		tmp12 = tmp0 + tmp2;
		tmp13 = tmp1 + tmp3;
		z = (tmp12 + tmp13) * Fix11;
		z += 1 << 10;

		tmp0 *= Fix15;
		tmp1 *= Fix30;
		tmp2 *= Fix20;
		tmp3 *= Fix02;
		tmp10 *= -Fix08;
		tmp11 *= -Fix25;
		tmp12 *= -Fix03;
		tmp13 *= -Fix19;

		tmp12 += z;
		tmp13 += z;

		b[y*8+1] = (tmp0 + tmp10 + tmp12) >> 11;
		b[y*8+3] = (tmp1 + tmp11 + tmp13) >> 11;
		b[y*8+5] = (tmp2 + tmp11 + tmp12) >> 11;
		b[y*8+7] = (tmp3 + tmp10 + tmp13) >> 11;
	}
	/* columns */
	for(x = 0; x < 8; x++) {
		tmp0 = b[0*8+x] + b[7*8+x];
		tmp1 = b[1*8+x] + b[6*8+x];
		tmp2 = b[2*8+x] + b[5*8+x];
		tmp3 = b[3*8+x] + b[4*8+x];

		if(sflag)
			tmp10 = (tmp0 + tmp3 + 1) << 1;
		else
			tmp10 = tmp0 + tmp3 + (1 << 1);
		tmp12 = tmp0 - tmp3;
		tmp11 = tmp1 + tmp2;
		tmp13 = tmp1 - tmp2;

		tmp0 = b[0*8+x] - b[7*8+x];
		tmp1 = b[1*8+x] - b[6*8+x];
		tmp2 = b[2*8+x] - b[5*8+x];
		tmp3 = b[3*8+x] - b[4*8+x];

		b[0*8+x] = (tmp10 + tmp11) >> 2;
		b[4*8+x] = (tmp10 - tmp11) >> 2;

		z = (tmp12 + tmp13) * Fix05;
		z += 1 << 14;
		b[2*8+x] = (z + tmp12*Fix07) >> 15;
		b[6*8+x] = (z - tmp13*Fix18) >> 15;

		tmp10 = tmp0 + tmp3;
		tmp11 = tmp1 + tmp2;
		tmp12 = tmp0 + tmp2;
		tmp13 = tmp1 + tmp3;
		z = (tmp12 + tmp13) * Fix11;
		z += 1 << 14;

		tmp0 *= Fix15;
		tmp1 *= Fix30;
		tmp2 *= Fix20;
		tmp3 *= Fix02;
		tmp10 *= -Fix08;
		tmp11 *= -Fix25;
		tmp12 *= -Fix03;
		tmp13 *= -Fix19;

		tmp12 += z;
		tmp13 += z;

		b[1*8+x] = (tmp0 + tmp10 + tmp12) >> 15;
		b[3*8+x] = (tmp1 + tmp11 + tmp13) >> 15;
		b[5*8+x] = (tmp2 + tmp11 + tmp12) >> 15;
		b[7*8+x] = (tmp3 + tmp10 + tmp13) >> 15;
	}
}

static int
csize(int coeff, int ac)
{
	int i, max;

	max = 1 << 10;
	if(!ac)
		max <<= 1;
	if(coeff < 0)
		coeff *= -1;
	if(coeff > max)
		sysfatal("csize: coeff too big: %d", coeff);
	i = ac? 1: 0;
	while(coeff >= (1<<i))
		i++;
	if(ac && (i < 1 || i > 10))
		sysfatal("csize: invalid ac ssss: %d", i);
	if(!ac && (i < 0 || i > 11))
		sysfatal("csize: invalid dc ssss: %d", i);
	return i;
}

static void
writebyte(Biobuf *fd)
{
	Bputc(fd, byte);
	if(byte == 0xff) /* byte stuffing */
		Bputc(fd, 0x00);
	byte = 0;
	nbyte = 7;
}

static void
writebits(Biobuf *fd, int co, int si)
{
	int i, bit;

	for(i = si-1; i >= 0; i--) {
		bit = (co >> i) & 0x1;
		byte |= bit << nbyte;
		nbyte--;
		if(nbyte < 0)
			writebyte(fd);
	}
}

static void
writecode(Biobuf *fd, int co, int si)
{
	if(si > 8) {
		writebits(fd, co>>8, si-8);
		si = 8;
	}
	writebits(fd, co, si);
}

static int
huf(Biobuf *fd, int *b, int pred, int chr, int sflag)
{
	int k, r, s, rs, si, co, dc, diff, zz[64], p, q, z;

	if(sflag) {
		for(k = 0; k < 64; k++) {
			p = b[zigzag[k]];
			q = qt[chr][zigzag[k]];
			zz[k] = p / q;
		}
	} else {
		for(k = 0; k < 64; k++) {
			p = b[k];
			q = (qt[chr][k] << 3);
			/* rounding */
			if(p >= 0)
				z = (p + (q >> 1)) / q;
			else
				z = -(-p + (q >> 1)) / q;
			zz[zigzag[k]] = z;
		}
	}

	/* dc coefficient */
	dc = zz[0];
	zz[0] = diff = dc - pred;

	s = csize(diff, 0);
	si = ehufsid[chr][s];
	co = ehufcod[chr][s];
	writecode(fd, co, si);
	if(diff < 0)
		diff -= 1;
	writecode(fd, diff, s);

	/* figure F.2 */
	for(k = 1, r = 0; k < 64; k++) {
		if(zz[k] == 0) {
			if(k < 63)
				r++;
			else {
				si = ehufsia[chr][0x00];
				co = ehufcoa[chr][0x00];
				writecode(fd, co, si);
			}
		} else {
			while(r > 15) {
				si = ehufsia[chr][0xf0];
				co = ehufcoa[chr][0xf0];
				writecode(fd, co, si);
				r -= 16;
			}
			/* figure F.3 */
			s = csize(zz[k], 1);
			rs = (r << 4) | s;
			si = ehufsia[chr][rs];
			co = ehufcoa[chr][rs];
			writecode(fd, co, si);
			if(zz[k] < 0)
				zz[k] -= 1;
			writecode(fd, zz[k], s);
			r = 0;
		}
	}
	return dc;
}

static char *
toycc1(int *y, int *cb, int *cr, int jx, int jy, int dx, int dy,
	int bpl, unsigned char *data, int ndata, int depth)
{
	int i, j, k, l, m, n, u, v, pos, pmask, nmask, pix;
	int r, g, b;

	m = 8 / depth;
	pmask = (1 << depth) - 1;
	nmask = 7 >> log2[depth];
	for(i = jy, k = 0; i < jy+8; i++) {
		v = min(i, dy-1);
		for(l = 0, j = jx/m; l < 8; l++, k++) {
			u = min(j, (dx-1)/m);
			n = l+jx >= dx? dx-jx-1: l;
			pos = v*bpl + u;
			if(pos >= ndata)
				return "WriteJPG: overflow";
			/* thanks writeppm */
			pix = (data[pos] >> depth*((nmask - n) &
				nmask)) & pmask;
			if(((n + 1) & nmask) == 0)
				j++;
			grey2rgb(&r, &g, &b, pix, depth);
			rgb2ycc(&y[k], &cb[k], &cr[k], r, g, b);
		}
	}
	return nil;
}

static char *
toycc2(int *y, int *cb, int *cr, int jx, int jy, int dx, int dy,
	int bpl, unsigned char *data, int ndata, int depth)
{
	int i, j, k, m, u, v, pos;

	m = depth / 8;
	for(i = jy, k = 0; i < jy+8; i++) {
		v = min(i, dy-1);
		for(j = jx*m; j < (jx+8)*m; j+=m, k++) {
			u = min(j, (dx-1)*m);
			pos = v*bpl + u;
			if(pos+m-1 >= ndata)
				return "WriteJPG: overflow";
			rgb2ycc(&y[k], &cb[k], &cr[k],
				data[pos+2*m/3],
				data[pos+m/3],
				data[pos]);
		}
	}
	return nil;
}

static char *
encode(Biobuf *fd, Rectangle r, unsigned char *data, uint32_t chan,
	int ndata, int kflag, int sflag)
{
	int k, x, y, dx, dy, depth, bpl, ncomp;
	int b[3][64], pred[3];
	char *err;
	char *(*toycc)(int *, int *, int *, int, int, int, int,
		int, unsigned char *, int, int);

	byte = 0;
	nbyte = 7;

	switch(chan) {
	case GREY1:
	case GREY2:
	case GREY4:
		toycc = toycc1;
		break;
	case GREY8:
	case RGB24:
		toycc = toycc2;
		break;
	default:
		return "WriteJPG: can't handle channel type";
	}

	/*
	* if dx or dy is not a multiple of 8,
	* the decoder should continue until reaching
	* the last mcu, even if the extra pixels go beyond
	* 0xffff. they are not shown anyway.
	*/
	dx = min(Dx(r), 0xffff);
	dy = min(Dy(r), 0xffff);
	depth = chantodepth(chan);
	bpl = bytesperline(r, depth);
	ncomp = kflag? 1: 3;
	memset(pred, 0, sizeof pred);
	for(x = 0, y = 0;;) {
		err = (*toycc)(b[0], b[1], b[2], x, y, dx, dy,
			bpl, data, ndata, depth);
		if(err != nil)
			return err;
		for(k = 0; k < ncomp; k++) {
			fdct(b[k], sflag);
			pred[k] = huf(fd, b[k], pred[k],
				k>0, sflag);
		}
		if((x += 8) >= dx) {
			if((y += 8) >= dy)
				break;
			x = 0;
		}
	}
	if(nbyte < 7) { /* bit padding */
		for(; nbyte >= 0; nbyte--)
			byte |= 0x1 << nbyte;
		writebyte(fd);
	}
	return err;
}

static void
makehuf(int *ehufco, int *ehufsi, int *bits, int *huffval, int n)
{
	int i, j, k, code, si, lastk, *huffcode, *huffsize;

	/* n+1 for lastk */
	if((huffcode = malloc((n+1)*sizeof *huffcode)) == nil)
		sysfatal("malloc: %r");
	if((huffsize = malloc((n+1)*sizeof *huffsize)) == nil)
		sysfatal("malloc: %r");
	/* figure C.1 */
	for(k = 0, i = 1, j = 1; i <= 16;) {
		if(j > bits[i-1]) { /* bits[i] in T.81: bug? */
			i++;
			j = 1;
		} else {
			huffsize[k++] = i;
			j++;
		}
	}
	huffsize[k] = 0;
	lastk = k;
	/* figure C.2 */
	for(k = 0, code = 0, si = huffsize[0];;) {
		do {
			huffcode[k++] = code++;
		} while(huffsize[k] == si);
		if(huffsize[k] == 0)
			break;
		while(huffsize[k] != si) {
			code <<= 1;
			si++;
		}
	}
	/* figure C.3 */
	for(k = 0; k < lastk; k++) {
		i = huffval[k];
		ehufco[i] = huffcode[k];
		ehufsi[i] = huffsize[k];
	}
	free(huffcode);
	free(huffsize);
}

static void
writejfif(Biobuf *fd, int dx, int dy)
{
	if(dx > 0xffff || dy > 0xffff)
		sysfatal("writejfif: dx or dy too big");
	Bputs(fd, 0xffe0);
	Bputs(fd, 0x0010);
	Bputc(fd, 0x4a);
	Bputc(fd, 0x46);
	Bputc(fd, 0x49);
	Bputc(fd, 0x46);
	Bputc(fd, 0x00);
	Bputs(fd, 0x0102);
	Bputc(fd, 0x01);
	Bputs(fd, dx);
	Bputs(fd, dy);
	Bputc(fd, 0x00);
	Bputc(fd, 0x00);
}

static void
writecomment(Biobuf *fd, char *com)
{
	int n;

	if(com != nil && com[0] != '\0') {
		n = min(strlen(com)+2, 0xffff);
		Bputs(fd, 0xfffe);
		Bputs(fd, n);
		Bwrite(fd, com, n-2);
	}
}

static void
writequant(Biobuf *fd, int tq, int sflag)
{
	int i, *p, *q;

	if(tq != 0x0 && tq != 0x1)
		sysfatal("writequant: invalid Tq");
	q = qt[tq];
	Bputs(fd, 0xffdb);
	Bputs(fd, 0x0043);
	Bputc(fd, (0x0<<4)|tq);
	p = sflag? zigzag: invzigzag;
	for(i = 0; i < 64; i++)
		Bputc(fd, q[p[i]]);
}

static void
writehuffman(Biobuf *fd, int tc, int th)
{
	int i, n, m, *b, *hv;

	if((tc != 0x0 && tc != 0x1) || (th != 0x0 && th != 0x1))
		sysfatal("writehuffman: invalid Tc or Th");
	n = 0x0013;
	if(tc == 0x0) {
		b = dcbits[th];
		hv = dchuffval[th];
		m = nelem(dchuffval[th]);
	} else {
		b = acbits[th];
		hv = achuffval[th];
		m = nelem(achuffval[th]);
	}
	Bputs(fd, 0xffc4);
	Bputs(fd, n+m);
	Bputc(fd, (tc<<4)|th);
	for(i = 0; i < 16; i++)
		Bputc(fd, b[i]);
	for(i = 0; i < m; i++)
		Bputc(fd, hv[i]);
}

static void
writeframe(Biobuf *fd, int y, int x, int kflag)
{
	int n, nf;

	nf = kflag? 0x01: 0x03;
	n = 0x0008 + 0x0003*nf;

	Bputs(fd, 0xffc0);
	Bputs(fd, n);
	Bputc(fd, 0x08);
	Bputs(fd, y);
	Bputs(fd, x);
	Bputc(fd, nf);

	/* Y component */
	Bputc(fd, 0x00);
	Bputc(fd, (0x1<<4)|0x1);
	Bputc(fd, 0x00);

	if(!kflag) {
		/* Cb component */
		Bputc(fd, 0x01);
		Bputc(fd, (0x1<<4)|0x1);
		Bputc(fd, 0x01);

		/* Cr component */
		Bputc(fd, 0x02);
		Bputc(fd, (0x1<<4)|0x1);
		Bputc(fd, 0x01);
	}
}

static void
writescan(Biobuf *fd, int kflag)
{
	int n, ns;

	ns = kflag? 0x01: 0x03;
	n = 0x0006 + 0x0002*ns;

	Bputs(fd, 0xffda);
	Bputs(fd, n);
	Bputc(fd, ns);

	/* Y component */
	Bputc(fd, 0x00);
	Bputc(fd, (0x0<<4)|0x0);

	if(!kflag) {
		/* Cb component */
		Bputc(fd, 0x01);
		Bputc(fd, (0x1<<4)|0x1);

		/* Cr component */
		Bputc(fd, 0x02);
		Bputc(fd, (0x1<<4)|0x1);
	}

	Bputc(fd, 0x00);
	Bputc(fd, 0x3f);
	Bputc(fd, (0x0<<4)|0x0);
}

static void
writeheader(Biobuf *fd, int dx, int dy, char *s, int kflag, int sflag)
{
	int i;

	dx = min(dx, 0xffff);
	dy = min(dy, 0xffff);

	Bputs(fd, 0xffd8);
	writejfif(fd, dx, dy);
	writecomment(fd, s);
	writequant(fd, 0, sflag);
	if(!kflag)
		writequant(fd, 1, sflag);
	writeframe(fd, dy, dx, kflag);
	for(i = 0; i < 2; i++) {
		writehuffman(fd, i, 0);
		if(!kflag)
			writehuffman(fd, i, 1);
	}
	writescan(fd, kflag);
}

static void
writetrailer(Biobuf *fd)
{
	Bputs(fd, 0xffd9);
}

static char *
writedata(Biobuf *fd, Image *i, Memimage *m, int kflag, int sflag)
{
	char *err;
	unsigned char *data;
	int ndata, depth;
	uint32_t chan;
	Rectangle r;

	if(m != nil) {
		r = m->r;
		depth = m->depth;
		chan = m->chan;
	} else {
		r = i->r;
		depth = i->depth;
		chan = i->chan;
	}

	/*
	* potentially one extra byte on each
	* end of each scan line
	*/
	ndata = Dy(r) * (2 + Dx(r)*depth/8);
	if((data = malloc(ndata)) == nil)
		return "WriteJPG: malloc failed";
	if(m != nil)
		ndata = unloadmemimage(m, r, data, ndata);
	else
		ndata = unloadimage(i, r, data, ndata);
	if(ndata < 0) {
		if((err = malloc(ERRMAX)) == nil) {
			free(data);
			return "WriteJPG: malloc failed";
		}
		snprint(err, ERRMAX, "WriteJPG: %r");
	} else
		err = encode(fd, r, data, chan, ndata, kflag, sflag);
	free(data);
	return err;
}

static char *
writejpg0(Biobuf *fd, Image *image, Memimage *memimage,
	Rectangle r, uint32_t chan, char *s, int kflag, int sflag)
{
	int i;
	char *err;

	switch(chan) {
	case GREY1:
	case GREY2:
	case GREY4:
	case GREY8:
		kflag = 1;
		break;
	case RGB24:
		break;
	default:
		return "WriteJPG: can't handle channel type";
	}
	for(i = 0; i < 2; i++) {
		memset(ehufcod[i], 0, sizeof ehufcod[i]);
		memset(ehufsid[i], 0, sizeof ehufsid[i]);
		memset(ehufcoa[i], 0, sizeof ehufcoa[i]);
		memset(ehufsia[i], 0, sizeof ehufsia[i]);
		makehuf(ehufcod[i], ehufsid[i], dcbits[i],
			dchuffval[i], nelem(dchuffval[i]));
		makehuf(ehufcoa[i], ehufsia[i], acbits[i],
			achuffval[i], nelem(achuffval[i]));
	}
	writeheader(fd, Dx(r), Dy(r), s, kflag, sflag);
	err = writedata(fd, image, memimage, kflag, sflag);
	writetrailer(fd);
	return err;
}

char *
writejpg(Biobuf *fd, Image *i, char *s, int kflag, int sflag)
{
	return writejpg0(fd, i, nil, i->r, i->chan, s, kflag, sflag);
}

char *
memwritejpg(Biobuf *fd, Memimage *m, char *s, int kflag, int sflag)
{
	return writejpg0(fd, nil, m, m->r, m->chan, s, kflag, sflag);
}
