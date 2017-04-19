/*
* code/documentation:
* http://partners.adobe.com/public/developer/en/tiff/TIFF6.pdf
* http://www.fileformat.info/format/tiff/egff.htm
* http://www.fileformat.info/mirror/egff/ch09_05.htm
* http://www.itu.int/rec/T-REC-T.4-199904-S/en
* http://www.itu.int/rec/T-REC-T.6-198811-I/en
*
* many thanks to paul bourke for a simple description of tiff:
* http://paulbourke.net/dataformats/tiff/
*
* copy-pasted fax codes and copy-pasted lzw encoding
* hash table implementation:
* http://www.remotesensing.org/libtiff/
*/
#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include "imagefile.h"

enum {
	Tbyte = 0x0001,
	Tascii = 0x0002,
	Tshort = 0x0003,
	Tlong = 0x0004,
	Trational = 0x0005,
	Tnocomp = 0x0001,
	Thuffman = 0x0002,
	Tt4enc = 0x0003,
	Tt6enc = 0x0004,
	Tlzw = 0x0005,
	Tpackbits = 0x8005
};

enum {
	Twidth,
	Tlength,
	Tbits,
	Tcomp,
	Tphoto,
	Tfill,
	Tdesc,
	Tstrips,
	Tsamples,
	Trows,
	Tcounts,
	Txres,
	Tyres,
	T4opt,
	Tresunit,
	Tpredictor,
	Tcolor
};

enum {
	Kpar = 2,
	Nfaxtab = 105
};

enum {
	Clrcode = 256,
	Eoicode = 257,
	Tabsz = 1<<12,
	Hshift = 13 - 8,
	Hsize = 9001L
};

typedef struct Tab Tab;
typedef struct Fax Fax;
typedef struct Hash Hash;
typedef struct Lzw Lzw;
typedef struct Pkb Pkb;
typedef struct Fld Fld;
typedef struct Tif Tif;

struct Tab {
	int len;
	int code;
	int run;
};

struct Fax {
	int st;
	Tab *tab[2];
	int byte;
	int nbyte;
	uint32_t *l1;
	uint32_t *l2;
	unsigned char *data;
	uint32_t ndata;
	uint32_t n;
};

struct Hash {
	long hash;
	unsigned short code;
};

struct Lzw {
	Hash hash[Hsize];
	int ntab;
	int len;
	int byte;
	int nbyte;
	unsigned char *data;
	uint32_t ndata;
	uint32_t n;
};

struct Pkb {
	unsigned char *data;
	uint32_t ndata;
	uint32_t n;
};

struct Fld {
	unsigned short tag;
	unsigned short typ;
};

struct Tif {
	uint32_t dx;
	uint32_t dy;
	unsigned short depth[3];
	unsigned short comp;
	uint32_t opt;
	char *(*compress)(Tif *);
	unsigned short photo;
	char *desc;
	uint32_t *strips;
	uint32_t nstrips;
	unsigned short samples;
	uint32_t rows;
	uint32_t *counts;
	unsigned short *color;
	uint32_t ncolor;
	unsigned char *data;
	uint32_t ndata;
	unsigned short nfld;
	int bpl;
};

static Fld flds[] = {
	[Twidth] {0x0100, Tlong},
	[Tlength] {0x0101, Tlong},
	[Tbits] {0x0102, Tshort},
	[Tcomp] {0x0103, Tshort},
	[Tphoto] {0x0106, Tshort},
	[Tfill] {0x010a, Tshort},
	[Tdesc] {0x010e, Tascii},
	[Tstrips] {0x0111, Tlong},
	[Tsamples] {0x0115, Tshort},
	[Trows] {0x0116, Tlong},
	[Tcounts] {0x0117, Tlong},
	[Txres] {0x011a, Trational},
	[Tyres] {0x011b, Trational},
	[T4opt] {0x0124, Tlong},
	[Tresunit] {0x0128, Tshort},
	[Tpredictor] {0x013d, Tshort},
	[Tcolor] {0x0140, Tshort}
};

static Tab faxwhite[Nfaxtab] = {
	{8, 0x35, 0}, /* 0011 0101 */
	{6, 0x7, 1}, /* 0001 11 */
	{4, 0x7, 2}, /* 0111 */
	{4, 0x8, 3}, /* 1000 */
	{4, 0xb, 4}, /* 1011 */
	{4, 0xc, 5}, /* 1100 */
	{4, 0xe, 6}, /* 1110 */
	{4, 0xf, 7}, /* 1111 */
	{5, 0x13, 8}, /* 1001 1 */
	{5, 0x14, 9}, /* 1010 0 */
	{5, 0x7, 10}, /* 0011 1 */
	{5, 0x8, 11}, /* 0100 0 */
	{6, 0x8, 12}, /* 0010 00 */
	{6, 0x3, 13}, /* 0000 11 */
	{6, 0x34, 14}, /* 1101 00 */
	{6, 0x35, 15}, /* 1101 01 */
	{6, 0x2a, 16}, /* 1010 10 */
	{6, 0x2b, 17}, /* 1010 11 */
	{7, 0x27, 18}, /* 0100 111 */
	{7, 0xc, 19}, /* 0001 100 */
	{7, 0x8, 20}, /* 0001 000 */
	{7, 0x17, 21}, /* 0010 111 */
	{7, 0x3, 22}, /* 0000 011 */
	{7, 0x4, 23}, /* 0000 100 */
	{7, 0x28, 24}, /* 0101 000 */
	{7, 0x2b, 25}, /* 0101 011 */
	{7, 0x13, 26}, /* 0010 011 */
	{7, 0x24, 27}, /* 0100 100 */
	{7, 0x18, 28}, /* 0011 000 */
	{8, 0x2, 29}, /* 0000 0010 */
	{8, 0x3, 30}, /* 0000 0011 */
	{8, 0x1a, 31}, /* 0001 1010 */
	{8, 0x1b, 32}, /* 0001 1011 */
	{8, 0x12, 33}, /* 0001 0010 */
	{8, 0x13, 34}, /* 0001 0011 */
	{8, 0x14, 35}, /* 0001 0100 */
	{8, 0x15, 36}, /* 0001 0101 */
	{8, 0x16, 37}, /* 0001 0110 */
	{8, 0x17, 38}, /* 0001 0111 */
	{8, 0x28, 39}, /* 0010 1000 */
	{8, 0x29, 40}, /* 0010 1001 */
	{8, 0x2a, 41}, /* 0010 1010 */
	{8, 0x2b, 42}, /* 0010 1011 */
	{8, 0x2c, 43}, /* 0010 1100 */
	{8, 0x2d, 44}, /* 0010 1101 */
	{8, 0x4, 45}, /* 0000 0100 */
	{8, 0x5, 46}, /* 0000 0101 */
	{8, 0xa, 47}, /* 0000 1010 */
	{8, 0xb, 48}, /* 0000 1011 */
	{8, 0x52, 49}, /* 0101 0010 */
	{8, 0x53, 50}, /* 0101 0011 */
	{8, 0x54, 51}, /* 0101 0100 */
	{8, 0x55, 52}, /* 0101 0101 */
	{8, 0x24, 53}, /* 0010 0100 */
	{8, 0x25, 54}, /* 0010 0101 */
	{8, 0x58, 55}, /* 0101 1000 */
	{8, 0x59, 56}, /* 0101 1001 */
	{8, 0x5a, 57}, /* 0101 1010 */
	{8, 0x5b, 58}, /* 0101 1011 */
	{8, 0x4a, 59}, /* 0100 1010 */
	{8, 0x4b, 60}, /* 0100 1011 */
	{8, 0x32, 61}, /* 0011 0010 */
	{8, 0x33, 62}, /* 0011 0011 */
	{8, 0x34, 63}, /* 0011 0100 */
	{5, 0x1b, 64}, /* 1101 1 */
	{5, 0x12, 128}, /* 1001 0 */
	{6, 0x17, 192}, /* 0101 11 */
	{7, 0x37, 256}, /* 0110 111 */
	{8, 0x36, 320}, /* 0011 0110 */
	{8, 0x37, 384}, /* 0011 0111 */
	{8, 0x64, 448}, /* 0110 0100 */
	{8, 0x65, 512}, /* 0110 0101 */
	{8, 0x68, 576}, /* 0110 1000 */
	{8, 0x67, 640}, /* 0110 0111 */
	{9, 0xcc, 704}, /* 0110 0110 0 */
	{9, 0xcd, 768}, /* 0110 0110 1 */
	{9, 0xd2, 832}, /* 0110 1001 0 */
	{9, 0xd3, 896}, /* 0110 1001 1 */
	{9, 0xd4, 960}, /* 0110 1010 0 */
	{9, 0xd5, 1024}, /* 0110 1010 1 */
	{9, 0xd6, 1088}, /* 0110 1011 0 */
	{9, 0xd7, 1152}, /* 0110 1011 1 */
	{9, 0xd8, 1216}, /* 0110 1100 0 */
	{9, 0xd9, 1280}, /* 0110 1100 1 */
	{9, 0xda, 1344}, /* 0110 1101 0 */
	{9, 0xdb, 1408}, /* 0110 1101 1 */
	{9, 0x98, 1472}, /* 0100 1100 0 */
	{9, 0x99, 1536}, /* 0100 1100 1 */
	{9, 0x9a, 1600}, /* 0100 1101 0 */
	{6, 0x18, 1664}, /* 0110 00 */
	{9, 0x9b, 1728}, /* 0100 1101 1 */
	{11, 0x8, 1792}, /* 0000 0001 000 */
	{11, 0xc, 1856}, /* 0000 0001 100 */
	{11, 0xd, 1920}, /* 0000 0001 101 */
	{12, 0x12, 1984}, /* 0000 0001 0010 */
	{12, 0x13, 2048}, /* 0000 0001 0011 */
	{12, 0x14, 2112}, /* 0000 0001 0100 */
	{12, 0x15, 2176}, /* 0000 0001 0101 */
	{12, 0x16, 2240}, /* 0000 0001 0110 */
	{12, 0x17, 2304}, /* 0000 0001 0111 */
	{12, 0x1c, 2368}, /* 0000 0001 1100 */
	{12, 0x1d, 2432}, /* 0000 0001 1101 */
	{12, 0x1e, 2496}, /* 0000 0001 1110 */
	{12, 0x1f, 2560}, /* 0000 0001 1111 */
	{12, 0x1, -1} /* 0000 0000 0001 */
};

static Tab faxblack[Nfaxtab] = {
	{10, 0x37, 0}, /* 0000 1101 11 */
	{3, 0x2, 1}, /* 010 */
	{2, 0x3, 2}, /* 11 */
	{2, 0x2, 3}, /* 10 */
	{3, 0x3, 4}, /* 011 */
	{4, 0x3, 5}, /* 0011 */
	{4, 0x2, 6}, /* 0010 */
	{5, 0x3, 7}, /* 0001 1 */
	{6, 0x5, 8}, /* 0001 01 */
	{6, 0x4, 9}, /* 0001 00 */
	{7, 0x4, 10}, /* 0000 100 */
	{7, 0x5, 11}, /* 0000 101 */
	{7, 0x7, 12}, /* 0000 111 */
	{8, 0x4, 13}, /* 0000 0100 */
	{8, 0x7, 14}, /* 0000 0111 */
	{9, 0x18, 15}, /* 0000 1100 0 */
	{10, 0x17, 16}, /* 0000 0101 11 */
	{10, 0x18, 17}, /* 0000 0110 00 */
	{10, 0x8, 18}, /* 0000 0010 00 */
	{11, 0x67, 19}, /* 0000 1100 111 */
	{11, 0x68, 20}, /* 0000 1101 000 */
	{11, 0x6c, 21}, /* 0000 1101 100 */
	{11, 0x37, 22}, /* 0000 0110 111 */
	{11, 0x28, 23}, /* 0000 0101 000 */
	{11, 0x17, 24}, /* 0000 0010 111 */
	{11, 0x18, 25}, /* 0000 0011 000 */
	{12, 0xca, 26}, /* 0000 1100 1010 */
	{12, 0xcb, 27}, /* 0000 1100 1011 */
	{12, 0xcc, 28}, /* 0000 1100 1100 */
	{12, 0xcd, 29}, /* 0000 1100 1101 */
	{12, 0x68, 30}, /* 0000 0110 1000 */
	{12, 0x69, 31}, /* 0000 0110 1001 */
	{12, 0x6a, 32}, /* 0000 0110 1010 */
	{12, 0x6b, 33}, /* 0000 0110 1011 */
	{12, 0xd2, 34}, /* 0000 1101 0010 */
	{12, 0xd3, 35}, /* 0000 1101 0011 */
	{12, 0xd4, 36}, /* 0000 1101 0100 */
	{12, 0xd5, 37}, /* 0000 1101 0101 */
	{12, 0xd6, 38}, /* 0000 1101 0110 */
	{12, 0xd7, 39}, /* 0000 1101 0111 */
	{12, 0x6c, 40}, /* 0000 0110 1100 */
	{12, 0x6d, 41}, /* 0000 0110 1101 */
	{12, 0xda, 42}, /* 0000 1101 1010 */
	{12, 0xdb, 43}, /* 0000 1101 1011 */
	{12, 0x54, 44}, /* 0000 0101 0100 */
	{12, 0x55, 45}, /* 0000 0101 0101 */
	{12, 0x56, 46}, /* 0000 0101 0110 */
	{12, 0x57, 47}, /* 0000 0101 0111 */
	{12, 0x64, 48}, /* 0000 0110 0100 */
	{12, 0x65, 49}, /* 0000 0110 0101 */
	{12, 0x52, 50}, /* 0000 0101 0010 */
	{12, 0x53, 51}, /* 0000 0101 0011 */
	{12, 0x24, 52}, /* 0000 0010 0100 */
	{12, 0x37, 53}, /* 0000 0011 0111 */
	{12, 0x38, 54}, /* 0000 0011 1000 */
	{12, 0x27, 55}, /* 0000 0010 0111 */
	{12, 0x28, 56}, /* 0000 0010 1000 */
	{12, 0x58, 57}, /* 0000 0101 1000 */
	{12, 0x59, 58}, /* 0000 0101 1001 */
	{12, 0x2b, 59}, /* 0000 0010 1011 */
	{12, 0x2c, 60}, /* 0000 0010 1100 */
	{12, 0x5a, 61}, /* 0000 0101 1010 */
	{12, 0x66, 62}, /* 0000 0110 0110 */
	{12, 0x67, 63}, /* 0000 0110 0111 */
	{10, 0xf, 64}, /* 0000 0011 11 */
	{12, 0xc8, 128}, /* 0000 1100 1000 */
	{12, 0xc9, 192}, /* 0000 1100 1001 */
	{12, 0x5b, 256}, /* 0000 0101 1011 */
	{12, 0x33, 320}, /* 0000 0011 0011 */
	{12, 0x34, 384}, /* 0000 0011 0100 */
	{12, 0x35, 448}, /* 0000 0011 0101 */
	{13, 0x6c, 512}, /* 0000 0011 0110 0 */
	{13, 0x6d, 576}, /* 0000 0011 0110 1 */
	{13, 0x4a, 640}, /* 0000 0010 0101 0 */
	{13, 0x4b, 704}, /* 0000 0010 0101 1 */
	{13, 0x4c, 768}, /* 0000 0010 0110 0 */
	{13, 0x4d, 832}, /* 0000 0010 0110 1 */
	{13, 0x72, 896}, /* 0000 0011 1001 0 */
	{13, 0x73, 960}, /* 0000 0011 1001 1 */
	{13, 0x74, 1024}, /* 0000 0011 1010 0 */
	{13, 0x75, 1088}, /* 0000 0011 1010 1 */
	{13, 0x76, 1152}, /* 0000 0011 1011 0 */
	{13, 0x77, 1216}, /* 0000 0011 1011 1 */
	{13, 0x52, 1280}, /* 0000 0010 1001 0 */
	{13, 0x53, 1344}, /* 0000 0010 1001 1 */
	{13, 0x54, 1408}, /* 0000 0010 1010 0 */
	{13, 0x55, 1472}, /* 0000 0010 1010 1 */
	{13, 0x5a, 1536}, /* 0000 0010 1101 0 */
	{13, 0x5b, 1600}, /* 0000 0010 1101 1 */
	{13, 0x64, 1664}, /* 0000 0011 0010 0 */
	{13, 0x65, 1728}, /* 0000 0011 0010 1 */
	{11, 0x8, 1792}, /* 0000 0001 000 */
	{11, 0xc, 1856}, /* 0000 0001 100 */
	{11, 0xd, 1920}, /* 0000 0001 101 */
	{12, 0x12, 1984}, /* 0000 0001 0010 */
	{12, 0x13, 2048}, /* 0000 0001 0011 */
	{12, 0x14, 2112}, /* 0000 0001 0100 */
	{12, 0x15, 2176}, /* 0000 0001 0101 */
	{12, 0x16, 2240}, /* 0000 0001 0110 */
	{12, 0x17, 2304}, /* 0000 0001 0111 */
	{12, 0x1c, 2368}, /* 0000 0001 1100 */
	{12, 0x1d, 2432}, /* 0000 0001 1101 */
	{12, 0x1e, 2496}, /* 0000 0001 1110 */
	{12, 0x1f, 2560}, /* 0000 0001 1111 */
	{12, 0x1, -1} /* 0000 0000 0001 */
};

static Tab faxcodes[] = {
	{4, 0x1, 0}, /* 0001 */
	{3, 0x1, 0}, /* 001 */
	{1, 0x1, 0}, /* 1 */
	{3, 0x2, 0}, /* 010 */
	{6, 0x2, 0}, /* 0000 10 */
	{7, 0x2, 0}, /* 0000 010 */
	{3, 0x3, 0}, /* 011 */
	{6, 0x3, 0}, /* 0000 11 */
	{7, 0x3, 0} /* 0000 011 */
};

static int typesizes[] = {0, 1, 1, 2, 4, 8};
static char memerr[] = "WriteTIF: malloc failed";

static int
put1(Biobuf *b, unsigned char c)
{
	return Bputc(b, c);
}

static int
put2(Biobuf *b, uint s)
{
	if(put1(b, s>>8) < 0)
		return -1;
	return put1(b, s);
}

static int
put4(Biobuf *b, uint32_t l)
{
	if(put2(b, l>>16) < 0)
		return -1;
	return put2(b, l);
}

static char *
nocomp(Tif * _)
{
	return nil;
}

static char *
faxputbyte(Fax *f)
{
	if(f->n >= f->ndata) {
		f->ndata *= 2;
		f->data = realloc(f->data,
			f->ndata*sizeof *f->data);
		if(f->data == nil)
			return memerr;
	}
	f->data[f->n++] = f->byte;
	f->byte = f->nbyte = 0;
	return nil;
}

static char *
faxputbit(Fax *f, int bit)
{
	f->byte = (f->byte << 1) | bit;
	f->nbyte++;
	return f->nbyte >= 8? faxputbyte(f): nil;
}

static char *
faxbytealign(Fax *f)
{
	char *err;

	err = nil;
	if(f->nbyte != 0) {
		f->byte <<= 8 - f->nbyte;
		err = faxputbyte(f);
	}
	return err;
}

static char *
faxputcode(Fax *f, Tab *tab)
{
	int i, bit;
	char *err;

	for(i = tab->len-1; i >= 0; i--) {
		bit = (tab->code >> i) & 0x1;
		if((err = faxputbit(f, bit)) != nil)
			return err;
	}
	return nil;
}

static int
faxgettab(int run)
{
	if(run >= 0) {
		if(run <= 64)
			return run;
		if(run <= 2560)
			return 64 + run/64 - 1;
	}
	return Nfaxtab - 1;
}

static char *
faxputrun(Fax *f, long run)
{
	char *err;
	Tab *tab, *p;

	tab = f->tab[f->st];
	p = &tab[faxgettab(2560)];
	while(run >= 2624) {
		if((err = faxputcode(f, p)) != nil)
			return err;
		run -= 2560;
	}
	if(run >= 64) {
		p = &tab[faxgettab(run)];
		if((err = faxputcode(f, p)) != nil)
			return err;
		run -= p->run;
	}
	p = &tab[faxgettab(run)];
	err = faxputcode(f, p);
	f->st ^= 1;
	return err;
}

static char *
faxputeol(Fax *f)
{
	return faxputcode(f, &f->tab[0][faxgettab(-1)]);
}

static char *
fax1d(Fax *f, uint32_t dx)
{
	uint32_t i;
	long run;
	char *err;

	f->st = 0;
	run = f->l2[0];
	for(i = 0;;) {
		if((err = faxputrun(f, run)) != nil)
			return err;
		if(f->l2[i++] >= dx)
			break;
		run = f->l2[i] - f->l2[i-1];
	}
	memmove(f->l1, f->l2, i*sizeof *f->l1);
	return nil;
}

static char *
fax2d(Fax *f, uint32_t dx)
{
	int j, v;
	uint32_t i;
	long a0, a1, a2, b1, b2;
	char *err;
	Tab *tab, *p;

	f->st = 0;
	a0 = a1 = -1;
	tab = faxcodes;
	for(i = 0, err = nil; err == nil;) {
		while(a1 <= a0)
			a1 = f->l2[i++];
		for(j = 0;; j++) {
			b1 = f->l1[j];
			if(b1 > a0 && f->st == j%2)
				break;
			if(b1 >= dx)
				break;
		}
		if((b2 = b1) < dx)
			b2 = f->l1[j+1];
		if(b2 < a1) {
			/* pass */
			p = &tab[0];
			err = faxputcode(f, p);
			a0 = b2;
		} else if(abs(v = a1-b1) < 3) {
			/* vertical */
			p = &tab[2+(v>0?3:0)+abs(v)];
			err = faxputcode(f, p);
			f->st ^= 1;
			a0 = a1;
		} else {
			/* horizontal */
			if(a0 < 0)
				a0 = 0;
			p = &tab[1];
			if((err = faxputcode(f, p)) != nil)
				return err;
			a2 = a1 < dx? f->l2[i++]: a1;
			if((err = faxputrun(f, a1-a0)) != nil)
				return err;
			err = faxputrun(f, a2-a1);
			a0 = a2;
		}
		if(a0 >= dx)
			break;
	}
	memmove(f->l1, f->l2, i*sizeof *f->l1);
	return err;
}

static char *
faxstrip(Tif *t, Fax *f, unsigned char *data, uint32_t n, uint32_t dx)
{
	int k, s, d1, two;
	uint32_t i, j, x;
	char *err;

	d1 = t->comp != Tt6enc;
	two = 0;
	if(t->comp == Tt4enc) {
		if((err = faxputeol(f)) != nil)
			return err;
		if(t->opt && (err = faxputbit(f, 1)) != nil)
			return err;
	}
	for(i = j = x = 0; i < n;) {
		s = 7 - x++%8;
		k = ((data[i] >> s) & 0x1) ^ 0x1;
		if(s == 0)
			i++;
		if(k != f->st) {
			f->l2[j++] = x - 1;
			f->st ^= 1;
		}
		if(x == dx) {
			f->l2[j] = dx;
			if(d1) {
				err = fax1d(f, dx);
				if(t->comp == Tt4enc &&
					t->opt) {
					two = Kpar - 1;
					d1 = 0;
				}
			} else {
				err = fax2d(f, dx);
				if(two > 0 && --two <= 0)
					d1 = 1;
			}
			if(err != nil)
				return err;
			if(t->comp == Thuffman)
				err = faxbytealign(f);
			else if(t->comp == Tt4enc &&
				t->opt) {
				if((err = faxputeol(f)) != nil)
					return err;
				err = faxputbit(f, d1);
			} else if(t->comp == Tt4enc)
				err = faxputeol(f);
			if(err != nil)
				return err;
			f->st = 0;
			if(s != 0)
				i++;
			x = 0;
			j = 0;
		}
	}
	if(t->comp == Tt4enc || t->comp == Tt6enc) {
		i = t->comp == Tt4enc? 5: 2;
		for(; i > 0; i--) {
			if((err = faxputeol(f)) != nil)
				return err;
			if(t->comp == Tt4enc && t->opt) {
				err = faxputbit(f, 1);
				if(err != nil)
					return err;
			}
		}
	}
	return faxbytealign(f);
}

static char *
fax(Tif *t)
{
	uint32_t i, m, n;
	char *err;
	unsigned char *data;
	Fax f;

	f.ndata = t->ndata;
	if((f.data = malloc(f.ndata*sizeof *f.data)) == nil)
		return memerr;
	f.l1 = mallocz((t->dx+1)*sizeof *f.l1, 1);
	f.l2 = mallocz((t->dx+1)*sizeof *f.l2, 1);
	if(f.l1 == nil || f.l2 == nil) {
		free(f.data);
		if(f.l1 != nil)
			free(f.l1);
		if(f.l2 != nil)
			free(f.l2);
		return memerr;
	}
	f.tab[0] = faxwhite;
	f.tab[1] = faxblack;
	f.n = f.byte = f.nbyte = 0;
	for(i = n = 0, data = t->data; i < t->nstrips; i++) {
		f.st = 0;
		f.l1[0] = t->dx;
		m = t->counts[i];
		if((err = faxstrip(t, &f, data, m, t->dx)) != nil) {
			if(f.data != nil)
				free(f.data);
			return err;
		}
		data += m;
		t->counts[i] = f.n - n;
		n = f.n;
	}
	free(t->data);
	free(f.l1);
	free(f.l2);
	t->data = f.data;
	t->ndata = f.n;
	return nil;
}

static void
lzwtabinit(Lzw *l)
{
	long i;
	Hash *hp;

	l->ntab = Eoicode + 1;
	l->len = 9;
	hp = &l->hash[Hsize-1];
	i = Hsize - 8;
	do {
		i -= 8;
		hp[-7].hash = -1;
		hp[-6].hash = -1;
		hp[-5].hash = -1;
		hp[-4].hash = -1;
		hp[-3].hash = -1;
		hp[-2].hash = -1;
		hp[-1].hash = -1;
		hp[0].hash = -1;
		hp -= 8;
	} while(i >= 0);
	for(i += 8; i > 0; i--, hp--)
		hp->hash = -1;
}

static char *
lzwputbyte(Lzw *l)
{
	if(l->n >= l->ndata) {
		l->ndata *= 2;
		l->data = realloc(l->data,
			l->ndata*sizeof *l->data);
		if(l->data == nil)
			return memerr;
	}
	l->data[l->n++] = l->byte;
	l->byte = l->nbyte = 0;
	return nil;
}

static char *
lzwbytealign(Lzw *l)
{
	char *err;

	err = nil;
	if(l->nbyte != 0) {
		l->byte <<= 8 - l->nbyte;
		err = lzwputbyte(l);
	}
	return err;
}

static char *
lzwputcode(Lzw *l, int code)
{
	int i, c;
	char *err;

	for(i = l->len-1; i >= 0; i--) {
		c = (code >> i) & 0x1;
		l->byte = (l->byte << 1) | c;
		l->nbyte++;
		if(l->nbyte >= 8) {
			if((err = lzwputbyte(l)) != nil)
				return err;
		}
	}
	return nil;
}

static void
predict1(Tif *t)
{
	int pix, b[8], d, m, n, j;
	uint32_t x, y;
	unsigned char *data, *p;

	p = t->data;
	d = *t->depth;
	m = (1 << d) - 1;
	n = 8 / d;
	for(y = 0; y < t->dy; y++) {
		data = p += t->bpl;
		for(x = t->bpl; x > 0; x--) {
			pix = *--data;
			for(j = 0; j < n; j++) {
				b[j] = (pix >> d*j) & m;
				if(j > 0)
					b[j-1] -= b[j];
			}
			if(x > 1)
				b[n-1] -= *(data-1) & m;
			for(j = pix = 0; j < n; j++)
				pix |= (b[j] & m) << d*j;
			*data = pix;
		}
	}
}

static void
predict8(Tif *t)
{
	uint32_t j, s, x, y;
	unsigned char *data, *p;

	p = t->data;
	s = t->samples;
	for(y = 0; y < t->dy; y++) {
		data = p += t->dx * s;
		for(x = t->dx; x > 1; x--) {
			for(j = 0; j < s; j++) {
				data--;
				*data -= *(data-s);
			}
		}
	}
}

static char *
lzwstrip(Lzw *l, unsigned char *data, uint32_t n)
{
	int k, h;
	long fcode, disp;
	uint32_t i;
	char *err;
	Hash *hp;
	unsigned short ent;

	if((err = lzwputcode(l, Clrcode)) != nil)
		return err;
	i = 0;
	ent = data[i++];
	for(; i < n; i++) {
		k = data[i];
		fcode = ((long)k << 12) + ent;
		h = (k << Hshift) ^ ent;
		hp = &l->hash[h];
		if(hp->hash == fcode) {
hit:
			ent = hp->code;
			continue;
		}
		if(hp->hash >= 0) {
			disp = h == 0? 1: Hsize - h;
			do {
				if((h -= disp) < 0)
					h += Hsize;
				hp = &l->hash[h];
				if(hp->hash == fcode)
					goto hit;
			} while(hp->hash >= 0);
		}
		if((err = lzwputcode(l, ent)) != nil)
			return err;
		ent = k;
		hp->hash = fcode;
		switch(hp->code = l->ntab) {
		case 511:
		case 1023:
		case 2047:
			l->len++;
			break;
		default:
			break;
		}
		if(l->ntab++ >= Tabsz-2) {
			err = lzwputcode(l, Clrcode);
			if(err != nil)
				return err;
			lzwtabinit(l);
		}
	}
	if((err = lzwputcode(l, ent)) != nil)
		return err;
	if((err = lzwputcode(l, Eoicode)) != nil)
		return err;
	return lzwbytealign(l);
}

static char *
lzw(Tif *t)
{
	uint32_t i, m, n;
	char *err;
	unsigned char *data;
	Lzw l;

	if(t->opt)
		*t->depth < 8? predict1(t): predict8(t);
	l.ndata = t->ndata;
	if((l.data = malloc(l.ndata*sizeof *l.data)) == nil)
		return memerr;
	l.n = l.byte = l.nbyte = 0;
	err = nil;
	for(i = n = 0, data = t->data; i < t->nstrips; i++) {
		lzwtabinit(&l);
		m = t->counts[i];
		if((err = lzwstrip(&l, data, m)) != nil)
			break;
		data += m;
		t->counts[i] = l.n - n;
		n = l.n;
	}
	if(err != nil) {
		if(l.data != nil)
			free(l.data);
		return err;
	}
	free(t->data);
	t->data = l.data;
	t->ndata = l.n;
	return nil;
}

static char *
pkbrow(Pkb *p, unsigned char *data, int ndata, long *buf)
{
	int b, repl;
	long i, j, k, n;
	uint32_t m;

	i = n = 0;
	buf[n++] = i;
	b = data[i++];
	if(i < ndata)
		repl = b == data[i]? 1: 0;
	else
		repl = 0;
	for(; i < ndata; i++) {
		k = data[i];
		j = labs(buf[n-1]);
		if(repl) {
			if(b != k) {
				repl ^= 1;
				buf[n++] = -i;
			}
		} else {
			if(b == k) {
				repl ^= 1;
				if(i-j > 1)
					buf[n++] = i - 1;
			}
		}
		b = k;
	}
	buf[n++] = repl? -i: i;
	for(i = 1; i < n;) {
		k = buf[i];
		j = labs(buf[i-1]);
		if(i < n-2 && k > 0 && buf[i+1] < 0 &&
			buf[i+2] > 0 && -buf[i+1]-k <= 2) {
			buf[i] = buf[i+1] = buf[i+2];
			continue;
		}
		if((b = labs(k) - j) > 128) {
			b = 128;
			buf[i-1] += buf[i-1] < 0? -b: b;
		} else
			i++;
		if(b == 0)
			continue;
		m = 1 + (k < 0? 1: b);
		if(p->n+m > p->ndata) {
			p->ndata = (p->n + m) * 2;
			p->data = realloc(p->data,
				p->ndata*sizeof *p->data);
			if(p->data == nil)
				return memerr;
		}
		if(k < 0) {
			p->data[p->n++] = 1 - b;
			p->data[p->n++] = data[j];
		} else {
			p->data[p->n++] = b - 1;
			memmove(p->data+p->n, data+j, b);
			p->n += b;
		}
	}
	return nil;
}

static char *
packbits(Tif *t)
{
	uint32_t i, j, n;
	char *err;
	unsigned char *data;
	long *buf;
	Pkb p;

	p.ndata = t->ndata;
	if((p.data = malloc(p.ndata*sizeof *p.data)) == nil)
		return memerr;
	if((buf = malloc((t->bpl+1)*sizeof *buf)) == nil) {
		free(p.data);
		return memerr;
	}
	p.n = 0;
	data = t->data;
	for(i = j = n = 0, err = nil; i < t->dy; i++) {
		if((err = pkbrow(&p, data, t->bpl, buf)) != nil)
			break;
		data += t->bpl;
		if(i%t->rows == t->rows-1) {
			t->counts[j++] = p.n - n;
			n = p.n;
		}
	}
	free(buf);
	if(err != nil) {
		if(p.data != nil)
			free(p.data);
		return err;
	}
	if(j < t->nstrips)
		t->counts[j] = p.n - n;
	free(t->data);
	t->data = p.data;
	t->ndata = p.n;
	return nil;
}

static char *
alloctif(Tif *t)
{
	int rgb;
	uint32_t i, count, n;

	count = t->ndata < 0x2000? t->ndata: 0x2000;
	t->rows = (count + t->bpl - 1) / t->bpl;
	if(t->comp == Tt4enc && t->opt) {
		if((n = t->rows%Kpar) != 0)
			t->rows += Kpar - n;
	}
	t->nstrips = (t->dy + t->rows - 1) / t->rows;
	t->strips = malloc(t->nstrips*sizeof *t->strips);
	if(t->strips == nil)
		return memerr;
	t->counts = malloc(t->nstrips*sizeof *t->counts);
	if(t->counts == nil) {
		free(t->strips);
		return memerr;
	}
	if(t->ncolor > 0) {
		t->color = malloc(t->ncolor*sizeof *t->color);
		if(t->color == nil) {
			free(t->strips);
			free(t->counts);
			return memerr;
		}
		for(i = 0; i < 256; i++) {
			rgb = cmap2rgb(i);
			t->color[i] = (rgb >> 16) & 0xff;
			t->color[i+256] = (rgb >> 8) & 0xff;
			t->color[i+256*2] = rgb & 0xff;
		}
	}
	count = t->rows * t->bpl;
	for(i = 0, n = t->ndata; i < t->nstrips-1; i++) {
		t->counts[i] = count;
		n -= count;
	}
	t->counts[i] = n;
	return nil;
}

static void
freetif(Tif *t)
{
	free(t->strips);
	free(t->counts);
	if(t->color != nil)
		free(t->color);
	free(t->data);
}

static int
typesize(int fld)
{
	return typesizes[flds[fld].typ];
}

static void
writefld(Biobuf *fd, int fld, uint32_t cnt, uint32_t val)
{
	put2(fd, flds[fld].tag);
	put2(fd, flds[fld].typ);
	put4(fd, cnt);
	put4(fd, val);
}

static void
writeflds(Biobuf *fd, Tif *t)
{
	int n;
	uint32_t i, off, slen, s, offs[7];

	slen = t->desc == nil? 0: strlen(t->desc) + 1;
	put2(fd, 0x4d4d);
	put2(fd, 0x002a);
	off = 0x00000008;
	memset(offs, 0, sizeof offs);
	n = 0;
	offs[n++] = off;
	if(t->samples > 1) {
		off += t->samples * typesize(Tbits);
		offs[n++] = off;
	}
	if(slen > 4) {
		off += slen * typesize(Tdesc);
		offs[n++] = off;
	}
	if(t->nstrips > 1) {
		off += t->nstrips * typesize(Tstrips);
		offs[n++] = off;
		off += t->nstrips * typesize(Tcounts);
		offs[n++] = off;
	}
	off += typesize(Txres);
	offs[n++] = off;
	off += typesize(Tyres);
	offs[n] = off;
	if(t->color != nil)
		off += t->ncolor * typesize(Tcolor);
	for(i = 0; i < t->nstrips-1; i++) {
		t->strips[i] = off;
		off += t->counts[i];
	}
	t->strips[i] = off;
	off += t->counts[i];
	put4(fd, off);
	if(t->samples > 1) {
		for(i = 0; i < t->samples; i++)
			put2(fd, t->depth[i]);
	}
	if(slen > 4) {
		Bwrite(fd, t->desc, slen-1);
		put1(fd, 0x00);
	}
	if(t->nstrips > 1) {
		for(i = 0; i < t->nstrips; i++)
			put4(fd, t->strips[i]);
		for(i = 0; i < t->nstrips; i++)
			put4(fd, t->counts[i]);
	}
	put4(fd, t->dx);
	put4(fd, 0x00000004);
	put4(fd, t->dy);
	put4(fd, 0x00000004);
	if(t->color != nil) {
		for(i = 0; i < t->ncolor; i++)
			put2(fd, t->color[i]);
	}
	Bwrite(fd, t->data, t->ndata);
	n = 0;
	put2(fd, t->nfld);
	writefld(fd, Twidth, 1, t->dx);
	writefld(fd, Tlength, 1, t->dy);
	if(t->samples > 1)
		writefld(fd, Tbits, t->samples, offs[n++]);
	else
		writefld(fd, Tbits, t->samples, *t->depth<<16);
	writefld(fd, Tcomp, 1, t->comp<<16);
	writefld(fd, Tphoto, 1, t->photo<<16);
	if(t->comp >= 2 && t->comp <= 4)
		writefld(fd, Tfill, 1, 1<<16);
	if(slen > 1) {
		if(slen <= 4) {
			for(i = s = 0; i < slen-1; i++)
				s = (s << 8) | t->desc[i];
			s <<= 8;
			writefld(fd, Tdesc, slen, s);
		} else
			writefld(fd, Tdesc, slen, offs[n++]);
	}
	if(t->nstrips > 1)
		writefld(fd, Tstrips, t->nstrips, offs[n++]);
	else
		writefld(fd, Tstrips, t->nstrips, *t->strips);
	if(t->samples > 1)
		writefld(fd, Tsamples, 1, t->samples<<16);
	writefld(fd, Trows, 1, t->rows);
	if(t->nstrips > 1)
		writefld(fd, Tcounts, t->nstrips, offs[n++]);
	else
		writefld(fd, Tcounts, t->nstrips, *t->counts);
	writefld(fd, Txres, 1, offs[n++]);
	writefld(fd, Tyres, 1, offs[n++]);
	if(t->comp == Tt4enc && t->opt)
		writefld(fd, T4opt, 1, 1);
	writefld(fd, Tresunit, 1, 2<<16);
	if(t->comp == Tlzw && t->opt)
		writefld(fd, Tpredictor, 1, 2<<16);
	if(t->color != nil)
		writefld(fd, Tcolor, t->ncolor, offs[n]);
	put4(fd, 0x00000000);
}

static char *
writedata(Biobuf *fd, Image *i, Memimage *m, Tif *t)
{
	char *err;
	unsigned char *data;
	int j, ndata, depth;
	Rectangle r;

	if(m != nil) {
		r = m->r;
		depth = m->depth;
	} else {
		r = i->r;
		depth = i->depth;
	}
	t->dx = Dx(r);
	t->dy = Dy(r);
	for(j = 0; j < t->samples; j++)
		t->depth[j] = depth / t->samples;
	/*
	* potentially one extra byte on each
	* end of each scan line
	*/
	ndata = t->dy * (2 + t->dx*depth/8);
	if((data = malloc(ndata)) == nil)
		return memerr;
	if(m != nil)
		ndata = unloadmemimage(m, r, data, ndata);
	else
		ndata = unloadimage(i, r, data, ndata);
	if(ndata < 0) {
		free(data);
		if((err = malloc(ERRMAX*sizeof *err)) == nil)
			return memerr;
		snprint(err, ERRMAX, "WriteTIF: %r");
	} else {
		t->data = data;
		t->ndata = ndata;
		t->bpl = bytesperline(r, depth);
		err = alloctif(t);
		if(err != nil) {
			freetif(t);
			return err;
		}
		if((err = (*t->compress)(t)) == nil)
			writeflds(fd, t);
		freetif(t);
	}
	return err;
}

static char *
writetif0(Biobuf *fd, Image *image, Memimage *memimage,
	uint32_t chan, char *s, int comp, int opt)
{
	Tif t;

	t.nfld = 11;
	t.color = nil;
	if((t.desc = s) != nil)
		t.nfld++;
	t.opt = opt;
	t.comp = comp;
	switch(chan) {
	case GREY1:
	case GREY4:
	case GREY8:
		t.photo = 1;
		t.samples = 1;
		t.ncolor = 0;
		break;
	case CMAP8:
		t.photo = 3;
		t.samples = 1;
		t.ncolor = 3 * 256;
		t.nfld++;
		break;
	case BGR24:
		t.photo = 2;
		t.samples = 3;
		t.ncolor = 0;
		t.nfld++;
		break;
	default:
		return "WriteTIF: can't handle channel type";
	}
	switch(t.comp) {
	case Tnocomp:
		t.compress = nocomp;
		break;
	case Thuffman:
	case Tt4enc:
	case Tt6enc:
		t.photo = 0;
		t.nfld++;
		if(t.comp == Tt4enc && t.opt)
			t.nfld++;
		t.compress = fax;
		break;
	case Tlzw:
		t.compress = lzw;
		if(t.opt)
			t.nfld++;
		break;
	case Tpackbits:
		t.compress = packbits;
		break;
	default:
		return "WriteTIF: unknown compression";
	}
	return writedata(fd, image, memimage, &t);
}

char *
writetif(Biobuf *fd, Image *i, char *s, int comp, int opt)
{
	return writetif0(fd, i, nil, i->chan, s, comp, opt);
}

char *
memwritetif(Biobuf *fd, Memimage *m, char *s, int comp, int opt)
{
	return writetif0(fd, nil, m, m->chan, s, comp, opt);
}
