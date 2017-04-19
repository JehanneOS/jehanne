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
* copy-pasted fax codes and lzw help:
* http://www.remotesensing.org/libtiff/
*/
#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include "imagefile.h"

enum {
	II = 0x4949, /* little-endian */
	MM = 0x4d4d, /* big-endian */
	TIF = 0x002a /* tiff magic number */
};

enum {
	Byte = 1,
	Short = 3,
	Long = 4
};

enum {
	Width = 0x0100,
	Length = 0x0101,
	Bits = 0x0102,

	Compression = 0x0103,
	Nocomp = 0x0001,
	Huffman = 0x0002,
	T4enc = 0x0003,
	T6enc = 0x0004,
	Lzwenc = 0x0005,
	Packbits = 0x8005,

	Photometric = 0x0106,
	Whitezero = 0x0000,
	Blackzero = 0x0001,
	Rgb = 0x0002,
	Palette = 0x0003,

	Fill = 0x010a,
	Strips = 0x0111,
	Orientation = 0x0112,
	Samples = 0x0115,
	Rows = 0x0116,
	Counts = 0x0117,
	Planar = 0x011c,
	T4opts = 0x0124,
	T6opts = 0x0125,
	Predictor = 0x13d,
	Color = 0x0140
};

enum {
	Nfaxcodes = 10,
	Nfaxtab = 105
};

enum {
	Clrcode = 256,
	Eoicode = 257,
	Tabsz = 1<<12
};

typedef struct Tab Tab;
typedef struct Fax Fax;
typedef struct Code Code;
typedef struct Lzw Lzw;
typedef struct Fld Fld;
typedef struct Tif Tif;

struct Tab {
	int len;
	int code;
	int run; /* run length */
};

struct Fax {
	uint32_t n;
	int m;
	int st; /* state */
	Tab *tab[2];
	int ntab; /* position in tab */
	Tab *eol;
	int eolfill;
	int (*getbit)(Fax *);
	uint32_t *l1;
	uint32_t *l2;
	uint32_t nl;
	unsigned char *data;
	uint32_t next; /* next strip offset in data */
};

struct Code {
	unsigned char val;
	Code *next;
};

struct Lzw {
	Code tab[Tabsz];
	int ntab;
	int len; /* code length */
	uint32_t n;
	int m;
	unsigned char *data;
	uint32_t next; /* next strip offset in data */
	/* remaining allocated codes */
	Code *first;
	Code *last;
};

struct Fld {
	uint tag;
	uint typ;
	uint32_t cnt;
	uint32_t off; /* value offset */
	uint32_t *val;
	uint32_t nval;
};

struct Tif {
	Biobuf *fd;
	uint end; /* endianness */
	unsigned char tmp[4];
	unsigned char *buf;
	uint32_t nbuf;
	int eof; /* reached end of image */
	uint32_t n; /* offset in buf array */
	uint32_t off;
	uint nfld;
	Fld *fld;
	uint32_t (*byte2)(unsigned char *);
	uint32_t (*byte4)(unsigned char *);

	/* field data */
	uint32_t dx;
	uint32_t dy;
	uint32_t depth;
	uint32_t comp;
	unsigned char *(*uncompress)(Tif *);
	uint32_t orientation;
	uint32_t photo;
	int (*decode)(Tif *, Rawimage *, unsigned char *);
	uint32_t fill;
	uint32_t *strips;
	uint32_t nstrips;
	uint32_t samples;
	uint32_t rows;
	uint32_t *counts;
	uint32_t ncounts;
	uint32_t planar;
	uint32_t *color; /* color map */
	uint32_t ncolor;
	uint32_t t4;
	uint32_t t6;
	uint32_t predictor;

	/* image data */
	unsigned char *data;
	uint32_t ndata;
};

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

static Tab faxwhite[Nfaxtab] = {
	{4, 0x7, 2}, /* 0111 */
	{4, 0x8, 3}, /* 1000 */
	{4, 0xb, 4}, /* 1011 */
	{4, 0xc, 5}, /* 1100 */
	{4, 0xe, 6}, /* 1110 */
	{4, 0xf, 7}, /* 1111 */
	{5, 0x12, 128}, /* 1001 0 */
	{5, 0x13, 8}, /* 1001 1 */
	{5, 0x14, 9}, /* 1010 0 */
	{5, 0x1b, 64}, /* 1101 1 */
	{5, 0x7, 10}, /* 0011 1 */
	{5, 0x8, 11}, /* 0100 0 */
	{6, 0x17, 192}, /* 0101 11 */
	{6, 0x18, 1664}, /* 0110 00 */
	{6, 0x2a, 16}, /* 1010 10 */
	{6, 0x2b, 17}, /* 1010 11 */
	{6, 0x3, 13}, /* 0000 11 */
	{6, 0x34, 14}, /* 1101 00 */
	{6, 0x35, 15}, /* 1101 01 */
	{6, 0x7, 1}, /* 0001 11 */
	{6, 0x8, 12}, /* 0010 00 */
	{7, 0x13, 26}, /* 0010 011 */
	{7, 0x17, 21}, /* 0010 111 */
	{7, 0x18, 28}, /* 0011 000 */
	{7, 0x24, 27}, /* 0100 100 */
	{7, 0x27, 18}, /* 0100 111 */
	{7, 0x28, 24}, /* 0101 000 */
	{7, 0x2b, 25}, /* 0101 011 */
	{7, 0x3, 22}, /* 0000 011 */
	{7, 0x37, 256}, /* 0110 111 */
	{7, 0x4, 23}, /* 0000 100 */
	{7, 0x8, 20}, /* 0001 000 */
	{7, 0xc, 19}, /* 0001 100 */
	{8, 0x12, 33}, /* 0001 0010 */
	{8, 0x13, 34}, /* 0001 0011 */
	{8, 0x14, 35}, /* 0001 0100 */
	{8, 0x15, 36}, /* 0001 0101 */
	{8, 0x16, 37}, /* 0001 0110 */
	{8, 0x17, 38}, /* 0001 0111 */
	{8, 0x1a, 31}, /* 0001 1010 */
	{8, 0x1b, 32}, /* 0001 1011 */
	{8, 0x2, 29}, /* 0000 0010 */
	{8, 0x24, 53}, /* 0010 0100 */
	{8, 0x25, 54}, /* 0010 0101 */
	{8, 0x28, 39}, /* 0010 1000 */
	{8, 0x29, 40}, /* 0010 1001 */
	{8, 0x2a, 41}, /* 0010 1010 */
	{8, 0x2b, 42}, /* 0010 1011 */
	{8, 0x2c, 43}, /* 0010 1100 */
	{8, 0x2d, 44}, /* 0010 1101 */
	{8, 0x3, 30}, /* 0000 0011 */
	{8, 0x32, 61}, /* 0011 0010 */
	{8, 0x33, 62}, /* 0011 0011 */
	{8, 0x34, 63}, /* 0011 0100 */
	{8, 0x35, 0}, /* 0011 0101 */
	{8, 0x36, 320}, /* 0011 0110 */
	{8, 0x37, 384}, /* 0011 0111 */
	{8, 0x4, 45}, /* 0000 0100 */
	{8, 0x4a, 59}, /* 0100 1010 */
	{8, 0x4b, 60}, /* 0100 1011 */
	{8, 0x5, 46}, /* 0000 0101 */
	{8, 0x52, 49}, /* 0101 0010 */
	{8, 0x53, 50}, /* 0101 0011 */
	{8, 0x54, 51}, /* 0101 0100 */
	{8, 0x55, 52}, /* 0101 0101 */
	{8, 0x58, 55}, /* 0101 1000 */
	{8, 0x59, 56}, /* 0101 1001 */
	{8, 0x5a, 57}, /* 0101 1010 */
	{8, 0x5b, 58}, /* 0101 1011 */
	{8, 0x64, 448}, /* 0110 0100 */
	{8, 0x65, 512}, /* 0110 0101 */
	{8, 0x67, 640}, /* 0110 0111 */
	{8, 0x68, 576}, /* 0110 1000 */
	{8, 0xa, 47}, /* 0000 1010 */
	{8, 0xb, 48}, /* 0000 1011 */
	{9, 0x98, 1472}, /* 0100 1100 0 */
	{9, 0x99, 1536}, /* 0100 1100 1 */
	{9, 0x9a, 1600}, /* 0100 1101 0 */
	{9, 0x9b, 1728}, /* 0100 1101 1 */
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
	{11, 0x8, 1792}, /* 0000 0001 000 */
	{11, 0xc, 1856}, /* 0000 0001 100 */
	{11, 0xd, 1920}, /* 0000 0001 101 */
	{12, 0x1, -1}, /* 0000 0000 0001 */
	{12, 0x12, 1984}, /* 0000 0001 0010 */
	{12, 0x13, 2048}, /* 0000 0001 0011 */
	{12, 0x14, 2112}, /* 0000 0001 0100 */
	{12, 0x15, 2176}, /* 0000 0001 0101 */
	{12, 0x16, 2240}, /* 0000 0001 0110 */
	{12, 0x17, 2304}, /* 0000 0001 0111 */
	{12, 0x1c, 2368}, /* 0000 0001 1100 */
	{12, 0x1d, 2432}, /* 0000 0001 1101 */
	{12, 0x1e, 2496}, /* 0000 0001 1110 */
	{12, 0x1f, 2560} /* 0000 0001 1111 */
};

static Tab faxblack[Nfaxtab] = {
	{2, 0x2, 3}, /* 10 */
	{2, 0x3, 2}, /* 11 */
	{3, 0x2, 1}, /* 010 */
	{3, 0x3, 4}, /* 011 */
	{4, 0x2, 6}, /* 0010 */
	{4, 0x3, 5}, /* 0011 */
	{5, 0x3, 7}, /* 0001 1 */
	{6, 0x4, 9}, /* 0001 00 */
	{6, 0x5, 8}, /* 0001 01 */
	{7, 0x4, 10}, /* 0000 100 */
	{7, 0x5, 11}, /* 0000 101 */
	{7, 0x7, 12}, /* 0000 111 */
	{8, 0x4, 13}, /* 0000 0100 */
	{8, 0x7, 14}, /* 0000 0111 */
	{9, 0x18, 15}, /* 0000 1100 0 */
	{10, 0x17, 16}, /* 0000 0101 11 */
	{10, 0x18, 17}, /* 0000 0110 00 */
	{10, 0x37, 0}, /* 0000 1101 11 */
	{10, 0x8, 18}, /* 0000 0010 00 */
	{10, 0xf, 64}, /* 0000 0011 11 */
	{11, 0x17, 24}, /* 0000 0010 111 */
	{11, 0x18, 25}, /* 0000 0011 000 */
	{11, 0x28, 23}, /* 0000 0101 000 */
	{11, 0x37, 22}, /* 0000 0110 111 */
	{11, 0x67, 19}, /* 0000 1100 111 */
	{11, 0x68, 20}, /* 0000 1101 000 */
	{11, 0x6c, 21}, /* 0000 1101 100 */
	{11, 0x8, 1792}, /* 0000 0001 000 */
	{11, 0xc, 1856}, /* 0000 0001 100 */
	{11, 0xd, 1920}, /* 0000 0001 101 */
	{12, 0x1, -1}, /* 0000 0000 0001 */
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
	{12, 0x24, 52}, /* 0000 0010 0100 */
	{12, 0x27, 55}, /* 0000 0010 0111 */
	{12, 0x28, 56}, /* 0000 0010 1000 */
	{12, 0x2b, 59}, /* 0000 0010 1011 */
	{12, 0x2c, 60}, /* 0000 0010 1100 */
	{12, 0x33, 320}, /* 0000 0011 0011 */
	{12, 0x34, 384}, /* 0000 0011 0100 */
	{12, 0x35, 448}, /* 0000 0011 0101 */
	{12, 0x37, 53}, /* 0000 0011 0111 */
	{12, 0x38, 54}, /* 0000 0011 1000 */
	{12, 0x52, 50}, /* 0000 0101 0010 */
	{12, 0x53, 51}, /* 0000 0101 0011 */
	{12, 0x54, 44}, /* 0000 0101 0100 */
	{12, 0x55, 45}, /* 0000 0101 0101 */
	{12, 0x56, 46}, /* 0000 0101 0110 */
	{12, 0x57, 47}, /* 0000 0101 0111 */
	{12, 0x58, 57}, /* 0000 0101 1000 */
	{12, 0x59, 58}, /* 0000 0101 1001 */
	{12, 0x5a, 61}, /* 0000 0101 1010 */
	{12, 0x5b, 256}, /* 0000 0101 1011 */
	{12, 0x64, 48}, /* 0000 0110 0100 */
	{12, 0x65, 49}, /* 0000 0110 0101 */
	{12, 0x66, 62}, /* 0000 0110 0110 */
	{12, 0x67, 63}, /* 0000 0110 0111 */
	{12, 0x68, 30}, /* 0000 0110 1000 */
	{12, 0x69, 31}, /* 0000 0110 1001 */
	{12, 0x6a, 32}, /* 0000 0110 1010 */
	{12, 0x6b, 33}, /* 0000 0110 1011 */
	{12, 0x6c, 40}, /* 0000 0110 1100 */
	{12, 0x6d, 41}, /* 0000 0110 1101 */
	{12, 0xc8, 128}, /* 0000 1100 1000 */
	{12, 0xc9, 192}, /* 0000 1100 1001 */
	{12, 0xca, 26}, /* 0000 1100 1010 */
	{12, 0xcb, 27}, /* 0000 1100 1011 */
	{12, 0xcc, 28}, /* 0000 1100 1100 */
	{12, 0xcd, 29}, /* 0000 1100 1101 */
	{12, 0xd2, 34}, /* 0000 1101 0010 */
	{12, 0xd3, 35}, /* 0000 1101 0011 */
	{12, 0xd4, 36}, /* 0000 1101 0100 */
	{12, 0xd5, 37}, /* 0000 1101 0101 */
	{12, 0xd6, 38}, /* 0000 1101 0110 */
	{12, 0xd7, 39}, /* 0000 1101 0111 */
	{12, 0xda, 42}, /* 0000 1101 1010 */
	{12, 0xdb, 43}, /* 0000 1101 1011 */
	{13, 0x4a, 640}, /* 0000 0010 0101 0 */
	{13, 0x4b, 704}, /* 0000 0010 0101 1 */
	{13, 0x4c, 768}, /* 0000 0010 0110 0 */
	{13, 0x4d, 832}, /* 0000 0010 0110 1 */
	{13, 0x52, 1280}, /* 0000 0010 1001 0 */
	{13, 0x53, 1344}, /* 0000 0010 1001 1 */
	{13, 0x54, 1408}, /* 0000 0010 1010 0 */
	{13, 0x55, 1472}, /* 0000 0010 1010 1 */
	{13, 0x5a, 1536}, /* 0000 0010 1101 0 */
	{13, 0x5b, 1600}, /* 0000 0010 1101 1 */
	{13, 0x64, 1664}, /* 0000 0011 0010 0 */
	{13, 0x65, 1728}, /* 0000 0011 0010 1 */
	{13, 0x6c, 512}, /* 0000 0011 0110 0 */
	{13, 0x6d, 576}, /* 0000 0011 0110 1 */
	{13, 0x72, 896}, /* 0000 0011 1001 0 */
	{13, 0x73, 960}, /* 0000 0011 1001 1 */
	{13, 0x74, 1024}, /* 0000 0011 1010 0 */
	{13, 0x75, 1088}, /* 0000 0011 1010 1 */
	{13, 0x76, 1152}, /* 0000 0011 1011 0 */
	{13, 0x77, 1216} /* 0000 0011 1011 1 */
};

static Tab faxcodes[Nfaxcodes] = {
	{1, 0x1, 0}, /* 1 */
	{3, 0x1, 0}, /* 001 */
	{3, 0x2, 0}, /* 010 */
	{3, 0x3, 0}, /* 011 */
	{4, 0x1, 0}, /* 0001 */
	{6, 0x2, 0}, /* 0000 10 */
	{6, 0x3, 0}, /* 0000 11 */
	{7, 0x2, 0}, /* 0000 010 */
	{7, 0x3, 0}, /* 0000 011 */
	{12, 0x1, -1} /* 0000 0000 0001 */
};

static int typesizes[] = {0, 1, 0, 2, 4};
static int vcodeval[] = {0, 0, 0, 1, 0, 0, 2, 3};

static uint32_t byte2le(unsigned char *);
static uint32_t byte4le(unsigned char *);
static uint32_t byte2be(unsigned char *);
static uint32_t byte4be(unsigned char *);
static void readdata(Tif *, uint32_t);
static void readnbytes(Tif *, uint32_t);
static uint32_t readbyte(Tif *);
static uint32_t readshort(Tif *);
static uint32_t readlong(Tif *);
static int gototif(Tif *, uint32_t);
static int readheader(Tif *);
static unsigned char *nocomp(Tif *);
static int getbit1(Fax *);
static int getbit2(Fax *);
static Tab *findtab(Fax *, int, int, Tab *, int);
static Tab *gettab(Fax *, int);
static Tab *geteol(Fax *);
static int faxfill(Fax *, unsigned char *, uint32_t, uint32_t *, uint32_t *, uint32_t, int);
static void fillbits(Fax *);
static int faxalloclines(Fax *);
static Tab *getfax1d(Fax *, unsigned char *, uint32_t, uint32_t *, uint32_t *, uint32_t);
static Tab *getfax2d(Fax *, unsigned char *, uint32_t, uint32_t *, uint32_t *, uint32_t);
static int faxstrip(Tif *, Fax *, unsigned char *, uint32_t, uint32_t, uint32_t *);
static unsigned char *fax(Tif *);
static void tabinit(Lzw *);
static Code *newcode(Lzw *, Code *);
static void listadd(Lzw *, Code *);
static Code *tabadd(Lzw *, Code *, Code *);
static int getcode(Lzw *);
static int wstr(unsigned char *, uint32_t, uint32_t *, Code *, long *);
static void predict1(Tif *, unsigned char *);
static void predict8(Tif *, unsigned char *);
static int lzwstrip(Lzw *, unsigned char *, uint32_t, uint32_t *, long);
static unsigned char *lzw(Tif *);
static unsigned char *packbits(Tif *);
static int faxdecode(Tif *, Rawimage *, unsigned char *);
static int greydecode(Tif *, Rawimage *, unsigned char *);
static int rgbdecode(Tif *, Rawimage *, unsigned char *);
static int paldecode(Tif *, Rawimage *, unsigned char *);
static int parsefield(Tif *, Fld *);
static int readfield(Tif *, Fld *);
static int checkfields(Tif *);
static int readstrips(Tif *);
static Rawimage *decode(Tif *);
static void freefields(Tif *);
static Rawimage *readslave(Tif *);

static uint32_t
byte2le(unsigned char *buf)
{
	return (buf[1] << 8) | buf[0];
}

static uint32_t
byte4le(unsigned char *buf)
{
	return (byte2le(buf+2) << 16) | byte2le(buf);
}

static uint32_t
byte2be(unsigned char *buf)
{
	return (buf[0] << 8) | buf[1];
}

static uint32_t
byte4be(unsigned char *buf)
{
	return (byte2be(buf) << 16) | byte2be(buf+2);
}

static void
readdata(Tif *t, uint32_t offset)
{
	long n, m;
	uint32_t size;

	if(offset < t->nbuf)
		offset = t->nbuf;
	m = offset + 4096 - t->nbuf;
	size = (m + t->nbuf) * sizeof *t->buf;
	if(t->buf == nil) {
		if((t->buf = malloc(size)) == nil)
			sysfatal("malloc: %r");
	} else {
		if((t->buf = realloc(t->buf, size)) == nil)
			sysfatal("realloc: %r");
	}
	if((n = Bread(t->fd, t->buf+t->nbuf, m)) < 0)
		sysfatal("Bread: %r");
	if(n != m)
		t->eof = 1;
	t->nbuf += n;
}

static void
readnbytes(Tif *t, uint32_t n)
{
	if(n <= 0 || n > 4)
		sysfatal("cannot read %lud bytes", n);
	if(t->n+n > t->nbuf) {
		if(t->eof)
			sysfatal("reached end of file");
		readdata(t, 0);
	}
	memmove(t->tmp, t->buf+t->n, n);
	t->n += n;
}

static uint32_t
readbyte(Tif *t)
{
	readnbytes(t, 1);
	return t->tmp[0];
}

static uint32_t
readshort(Tif *t)
{
	readnbytes(t, 2);
	return (*t->byte2)(t->tmp);
}

static uint32_t
readlong(Tif *t)
{
	readnbytes(t, 4);
	return (*t->byte4)(t->tmp);
}

static int
gototif(Tif *t, uint32_t n)
{
	if(n < 8) {
		werrstr("offset pointing to header");
		return -1;
	}
	if(n > t->nbuf)
		readdata(t, n);
	t->n = n;
	return 0;
}

static int
readheader(Tif *t)
{
	uint n;

	t->end = readshort(t);
	switch(t->end) {
	case II:
		break;
	case MM:
		t->byte2 = byte2be;
		t->byte4 = byte4be;
		break;
	default:
		werrstr("illegal byte order: %#.4x", t->end);
		return -1;
	}
	if((n = readshort(t)) != TIF) {
		werrstr("illegal tiff magic: %#.4x", n);
		return -1;
	}
	t->off = readlong(t);
	return gototif(t, t->off);
}

static unsigned char *
nocomp(Tif *t)
{
	return t->data;
}

static int
getbit1(Fax *f)
{
	int bit;

	if(f->n >= f->next)
		return -1;
	bit = (f->data[f->n] >> f->m) & 0x1;
	f->m--;
	if(f->m < 0) {
		f->n++;
		f->m = 7;
	}
	return bit;
}

static int
getbit2(Fax *f)
{
	int bit;

	if(f->n >= f->next)
		return -1;
	bit = (f->data[f->n] >> f->m) & 0x1;
	f->m++;
	if(f->m >= 8) {
		f->n++;
		f->m = 0;
	}
	return bit;
}

static Tab *
findtab(Fax *f, int code, int len, Tab *tab, int max)
{
	Tab *p;

	while(f->ntab < max) {
		p = &tab[f->ntab];
		if(p->len > len)
			break;
		if(p->code == code) {
			f->ntab = 0;
			return p;
		}
		f->ntab++;
	}
	return nil;
}

static Tab *
gettab(Fax *f, int mode)
{
	int i, n, maxlen, bit, code;
	Tab *p, *tab;

	code = 0;
	if(mode) {
		n = Nfaxcodes;
		tab = faxcodes;
	} else {
		n = Nfaxtab;
		tab = f->tab[f->st];
	}
	maxlen = tab[n-1].len;
	for(i = 1; i <= maxlen; i++) {
		if((bit = (*f->getbit)(f)) < 0) {
			f->st = -1;
			return nil;
		}
		code = (code << 1) | bit;
		if((p = findtab(f, code, i, tab, n)) != nil)
			return p;
	}
	werrstr("code not found");
	return nil;
}

static Tab *
geteol(Fax *f)
{
	int i, bit;
	Tab *p;

	if(f->eol == nil) {
		if(f->eolfill) {
			for(i = 0; i < 4; i++) {
				if((*f->getbit)(f) < 0) {
					f->st = -1;
					return nil;
				}
			}
		}
		if((p = gettab(f, 0)) == nil || p->run >= 0) {
			werrstr("first eol");
			return nil;
		}
		f->eol = p;
		return p;
	}
	for(i = 0; (bit = (*f->getbit)(f)) == 0; i++)
		;
	if(bit < 0) {
		f->st = -1;
		return nil;
	}
	if(i < 11) {
		werrstr("eol");
		return nil;
	}
	return f->eol;
}

static int
faxfill(Fax *f, unsigned char *data, uint32_t size, uint32_t *i, uint32_t *x, uint32_t dx,
	int n)
{
	if((*x += n) > dx) {
		werrstr("fax row overflow");
		return -1;
	}
	if((*i += n) > size)
		return -1;
	if(f->st != 0)
		memset(data+*i-n, f->st, n);
	return 0;
}

static void
fillbits(Fax *f)
{
	if(f->getbit == getbit1) {
		if(f->m != 7) {
			f->n++;
			f->m = 7;
		}
	} else {
		if(f->m != 0) {
			f->n++;
			f->m = 0;
		}
	}
}

static int
faxalloclines(Fax *f)
{
	f->nl *= 2;
	f->l1 = realloc(f->l1, f->nl*sizeof *f->l1);
	if(f->l1 == nil)
		return -1;
	f->l2 = realloc(f->l2, f->nl*sizeof *f->l2);
	if(f->l2 == nil) {
		free(f->l1);
		return -1;
	}
	return 0;
}

static Tab *
getfax1d(Fax *f, unsigned char *data, uint32_t size, uint32_t *i, uint32_t *x,
	uint32_t dx)
{
	int j, n;
	Tab *p;

	for(j = n = 0; *x < dx;) {
		if((p = gettab(f, 0)) == nil)
			return nil;
		if((n = p->run) < 0) {
			f->l1[j] = dx;
			return f->eol;
		}
		if(faxfill(f, data, size, i, x, dx, n) < 0)
			return nil;
		if(n < 64) {
			f->l1[j++] = *x;
			f->st ^= 1;
		}
		if(j >= f->nl)
			faxalloclines(f);
	}
	if(n >= 64) {
		f->l1[j] = dx;
		if((p = gettab(f, 0)) == nil)
			return nil;
		if((n = p->run) < 0)
			return f->eol;
		if(n != 0) {
			werrstr("no terminating code");
			return nil;
		}
	}
	return nil;
}

static Tab *
getfax2d(Fax *f, unsigned char *data, uint32_t size, uint32_t *i, uint32_t *x,
	uint32_t dx)
{
	int j, k, n, code, len, v;
	long a0, a1, b1, b2;
	Tab *p;

	a0 = -1;
	for(j = 0; *x < dx;) {
		for(k = 0;; k++) {
			b1 = f->l1[k];
			if(b1 > a0 && f->st == k%2)
				break;
			if(b1 >= dx)
				break;
		}
		if((b2 = b1) < dx)
			b2 = f->l1[k+1];
		if((p = gettab(f, 1)) == nil)
			return nil;
		/* early eofb */
		if(p->run < 0) {
			f->st = -1;
			return nil;
		}
		if(j+1 >= f->nl)
			faxalloclines(f);
		len = p->len;
		code = p->code;
		if(code == 1 && len == 3) {
			/* horizontal */
			for(k = 0; k < 2;) {
				if((p = gettab(f, 0)) == nil)
					return nil;
				if((n = p->run) < 0) {
					werrstr("2d eol");
					return nil;
				}
				if(faxfill(f, data, size, i, x,
					dx, n) < 0)
					return nil;
				if(n < 64) {
					f->l2[j++] = *x;
					f->st ^= 1;
					k++;
				}
			}
		} else if(code == 1 && len == 4) {
			/* pass */
			n = b2 - *x;
			if(faxfill(f, data, size, i, x, dx, n) < 0)
				return nil;
			if(*x == dx)
				f->l2[j++] = *x;
		} else {
			/* vertical */
			switch(code) {
			case 1:
			case 2:
				v = -vcodeval[len];
				break;
			case 3:
				v = vcodeval[len];
				break;
			default:
				werrstr("mode");
				return nil;
			}
			a1 = b1 + v;
			n = a1 - *x;
			if(faxfill(f, data, size, i, x, dx, n) < 0)
				return nil;
			f->l2[j++] = *x;
			f->st ^= 1;
		}
		a0 = *x;
	}
	memmove(f->l1, f->l2, j*sizeof *f->l1);
	return nil;
}

static int
faxstrip(Tif *t, Fax *f, unsigned char *data, uint32_t size, uint32_t rows,
	uint32_t *i)
{
	int d1;
	uint32_t x, y;
	Tab *p;

	d1 = t->comp != T6enc;
	p = nil;
	for(x = y = 0; x < t->dx || y < rows;) {
		f->st = 0;
		if(t->comp == T4enc) {
			if(p == nil && geteol(f) == nil) {
				if(f->st >= 0)
					return -1;
				break;
			}
			if(y > 0) {
				*i += t->dx - x;
				if(*i > size)
					break;
			}
			if(t->t4 & 1) {
				d1 = (*f->getbit)(f);
				if(d1 < 0)
					break;
			}
		}
		x = 0;
		y++;
		if(d1) {
			p = getfax1d(f, data, size, i,
				&x, t->dx);
		} else {
			p = getfax2d(f, data, size, i,
				&x, t->dx);
		}
		if(t->comp == Huffman)
			fillbits(f);
		if(p == nil && x != t->dx) {
			if(f->st >= 0)
				return -1;
			if(x > t->dx)
				return -1;
			break;
		}
	}
	if(*i > size) {
		werrstr("fax data overflow");
		return -1;
	}
	return 0;
}

/* i've encountered t4 images that did not have rtcs */
static unsigned char *
fax(Tif *t)
{
	int m;
	uint32_t i, j, datasz, r, dy;
	unsigned char *data;
	Fax f;

	datasz = t->dx * t->dy * sizeof *data;
	data = mallocz(datasz, 1);
	f.nl = t->dx + 1;
	f.l1 = mallocz(f.nl*sizeof *f.l1, 1);
	f.l2 = mallocz(f.nl*sizeof *f.l2, 1);
	if(data == nil || f.l1 == nil || f.l2 == nil) {
		free(t->data);
		if(data != nil)
			free(data);
		if(f.l1 != nil)
			free(f.l1);
		if(f.l2 != nil)
			free(f.l2);
		return nil;
	}
	if(t->fill == 1) {
		f.getbit = getbit1;
		m = 7;
	} else {
		f.getbit = getbit2;
		m = 0;
	}
	f.tab[0] = faxwhite;
	f.tab[1] = faxblack;
	f.ntab = 0;
	f.eol = nil;
	if(t->comp == T4enc && t->t4 & (1<<1))
		f.eolfill = 1;
	else
		f.eolfill = 0;
	f.data = t->data;
	for(i = j = 0, dy = t->dy; i < t->nstrips; i++) {
		f.l1[0] = t->dx;
		f.n = t->strips[i];
		f.m = m;
		if(i < t->nstrips-1)
			f.next = t->strips[i+1];
		else
			f.next = t->ndata;
		r = dy < t->rows? dy: t->rows;
		if(faxstrip(t, &f, data, datasz, r, &j) < 0)
			break;
		dy -= t->rows;
	}
	if(i < t->nstrips) {
		free(data);
		data = nil;
	}
	free(t->data);
	free(f.l1);
	free(f.l2);
	return data;
}

static void
tabinit(Lzw *l)
{
	l->ntab = Eoicode + 1;
	l->len = 9;
}

static Code *
newcode(Lzw *l, Code *p)
{
	Code *q;

	if(p == nil)
		return nil;
	if(l->first != nil) {
		q = l->first;
		if((l->first = l->first->next) == nil)
			l->last = nil;
	} else if((q = malloc(sizeof *q)) == nil)
		return nil;
	q->val = p->val;
	q->next = nil;
	return q;
}

static void
listadd(Lzw *l, Code *p)
{
	if(p == nil)
		return;
	if(l->last != nil)
		l->last->next = p;
	else
		l->first = l->last = p;
	while(l->last->next != nil)
		l->last = l->last->next;
}

static Code *
tabadd(Lzw *l, Code *p, Code *q)
{
	Code *r, *s;

	r = s = &l->tab[l->ntab];
	switch(l->ntab) {
	case 510:
	case 1022:
	case 2046:
		l->len++;
		break;
	default:
		break;
	}
	if(l->ntab++ >= Tabsz-3) {
		werrstr("lzw table full");
		return nil;
	}
	s->val = p->val;
	while((p = p->next) != nil) {
		if(s->next != nil)
			s->next->val = p->val;
		else if((s->next = newcode(l, p)) == nil)
			return nil;
		s = s->next;
	}
	if(s->next != nil) {
		s->next->val = q->val;
		s = s->next;
		if(s->next != nil) {
			listadd(l, s->next);
			s->next = nil;
		}
	} else if((s->next = newcode(l, q)) == nil)
		return nil;
	return r;
}

static int
getcode(Lzw *l)
{
	int i, c, code;

	if(l->n >= l->next) {
eof:
		werrstr("lzw eof");
		return -1;
	}
	code = 0;
	for(i = l->len-1; i >= 0; i--) {
		c = (l->data[l->n] >> l->m) & 0x1;
		code |= c << i;
		l->m--;
		if(l->m < 0) {
			l->m = 7;
			if(++l->n >= l->next && i > 0)
				goto eof;
		}
	}
	return code;
}

static int
wstr(unsigned char *data, uint32_t size, uint32_t *i, Code *p, long *striplen)
{
	for(; p != nil; p = p->next, ++*i, --*striplen) {
		if(*i >= size || *striplen < 0) {
			werrstr("lzw overflow");
			return -1;
		}
		data[*i] = p->val;
	}
	return 0;
}

static void
predict1(Tif *t, unsigned char *data)
{
	int bpl, pix, b[8], d, m, n, j;
	uint32_t x, y;

	d = t->depth;
	bpl = bytesperline(Rect(0, 0, t->dx, t->dy), d);
	m = (1 << d) - 1;
	n = 8 / d;
	for(y = 0; y < t->dy; y++) {
		for(x = 0; x < bpl; x++, data++) {
			pix = *data;
			b[n-1] = (pix >> d*(n-1)) & m;
			if(x > 0)
				b[n-1] += *(data-1) & m;
			for(j = n-2; j >= 0; j--) {
				b[j] = (pix >> d*j) & m;
				b[j] += b[j+1];
			}
			for(j = pix = 0; j < n; j++)
				pix |= (b[j] & m) << d*j;
			*data = pix;
		}
	}
}

static void
predict8(Tif *t, unsigned char *data)
{
	uint32_t j, s, x, y;

	s = t->samples;
	for(y = 0; y < t->dy; y++) {
		for(x = 1, data += s; x < t->dx; x++) {
			for(j = 0; j < s; j++, data++)
				*data += *(data-s);
		}
	}
}

static int
lzwstrip(Lzw *l, unsigned char *data, uint32_t size, uint32_t *i, long striplen)
{
	int c, oc;
	Code *p, *q;

	if((c = getcode(l)) != Clrcode) {
		werrstr("clear code");
		return -1;
	}
	for(oc = -1; c != Eoicode;) {
		if(c < 0)
			return -1;
		if(c == Clrcode) {
			if(oc >= 0)
				tabinit(l);
			if((c = getcode(l)) == Eoicode)
				break;
			if(c < 0)
				return -1;
			if(c >= l->ntab) {
				werrstr("table overflow");
				return -1;
			}
			if(wstr(data, size, i, &l->tab[c],
				&striplen) < 0)
				return -1;
		} else if(c < l->ntab) {
			p = &l->tab[c];
			if(wstr(data, size, i, p,
				&striplen) < 0)
				return -1;
			q = &l->tab[oc];
			if(tabadd(l, q, p) == nil)
				return -1;
		} else if(c == l->ntab) {
			q = &l->tab[oc];
			if((p = tabadd(l, q, q)) == nil)
				return -1;
			if(wstr(data, size, i, p,
				&striplen) < 0)
				return -1;
		} else {
			werrstr("table overflow");
			return -1;
		}
		if(striplen <= 0)
			break;
		oc = c;
		c = getcode(l);
	}
	return 0;
}

static unsigned char *
lzw(Tif *t)
{
	uint32_t i, j, size, r, dy;
	long striplen;
	unsigned char *data;
	Lzw l;
	Code *p, *q;

	size = ((t->dx*t->dy*t->depth + 7) / 8) * sizeof *data;
	if((data = malloc(size)) == nil) {
		free(t->data);
		return nil;
	}
	for(i = 0; i < Tabsz; i++) {
		l.tab[i].val = i;
		l.tab[i].next = nil;
	}
	l.data = t->data;
	l.first = l.last = nil;
	for(i = j = 0, dy = t->dy; i < t->nstrips; i++) {
		tabinit(&l);
		l.n = t->strips[i];
		l.m = 7;
		if(i < t->nstrips-1)
			l.next = t->strips[i+1];
		else
			l.next = t->ndata;
		r = dy < t->rows? dy: t->rows;
		striplen = (t->dx*r*t->depth + 7) / 8;
		if(lzwstrip(&l, data, size, &j, striplen) < 0)
			break;
		dy -= t->rows;
	}
	if(i < t->nstrips) {
		free(data);
		data = nil;
	}
	for(i = 0; i < Tabsz; i++) {
		for(p = l.tab[i].next; (q = p) != nil;) {
			p = p->next;
			free(q);
		}
	}
	for(p = l.first; (q = p) != nil;) {
		p = p->next;
		free(q);
	}
	free(t->data);
	if(data != nil && t->predictor == 2) {
		if(t->depth < 8)
			predict1(t, data);
		else
			predict8(t, data);
	}
	return data;
}

static unsigned char *
packbits(Tif *t)
{
	char n;
	uint32_t i, j, k, size;
	unsigned char *data;

	size = ((t->dx*t->dy*t->depth + 7) / 8) * sizeof *data;
	if((data = malloc(size)) == nil) {
		free(t->data);
		return nil;
	}
	for(i = 0, j = 0; i < t->ndata;) {
		n = (char)t->data[i++];
		if(n >= 0) {
			k = n + 1;
			if((j += k) > size || (i += k) > t->ndata)
				break;
			memmove(data+j-k, t->data+i-k, k);
		} else if(n > -128 && n < 0) {
			k = j - n + 1;
			if(k > size || i >= t->ndata)
				break;
			for(; j < k; j++)
				data[j] = t->data[i];
			i++;
		}
	}
	if(i < t->ndata) {
		werrstr("packbits overflow");
		free(data);
		data = nil;
	}
	free(t->data);
	return data;
}

static int
faxdecode(Tif *t, Rawimage *im, unsigned char *data)
{
	uint32_t n;

	for(n = 0; n < im->chanlen; n++) {
		if(t->photo == Whitezero)
			data[n] ^= 1;
		im->chans[0][n] = data[n] * 0xff;
	}
	return 0;
}

static int
greydecode(Tif *t, Rawimage *im, unsigned char *data)
{
	int pix, pmask, xmask;
	uint32_t i, n, x, y;

	pmask = (1 << t->depth) - 1;
	xmask = 7 >> log2[t->depth];
	for(y = 0, n = 0; y < t->dy; y++) {
		i = y * bytesperline(im->r, t->depth);
		for(x = 0; x < t->dx; x++, n++) {
			if(n >= im->chanlen) {
				werrstr("grey overflow");
				return -1;
			}
			pix = (data[i] >> t->depth*((xmask -
				x) & xmask)) & pmask;
			if(((x + 1) & xmask) == 0)
				i++;
			if(t->photo == Whitezero)
				pix ^= pmask;
			pix = (pix * 0xff) / pmask;
			im->chans[0][n] = pix;
		}
	}
	return 0;
}

static int
rgbdecode(Tif *t, Rawimage *im, unsigned char *data)
{
	uint32_t i, n, x, y;

	for(y = 0, n = 0; y < t->dy; y++) {
		for(x = 0; x < t->dx; x++, n += 3) {
			if(n >= im->chanlen) {
				werrstr("rgb overflow");
				return -1;
			}
			i = (y*t->dx + x) * 3;
			im->chans[0][n] = data[i+2];
			im->chans[0][n+1] = data[i+1];
			im->chans[0][n+2] = data[i];
		}
	}
	return 0;
}

static int
paldecode(Tif *t, Rawimage *im, unsigned char *data)
{
	int pix, pmask, xmask;
	uint32_t i, n, x, y, *r, *g, *b, max;

	pmask = (1 << t->depth) - 1;
	xmask = 7 >> log2[t->depth];
	for(i = 0, max = 1; i < t->ncolor; i++) {
		if(t->color[i] > max)
			max = t->color[i];
	}
	for(i = 0; i < t->ncolor; i++)
		t->color[i] = (t->color[i] * 0xff) / max;
	r = t->color;
	g = r + pmask + 1;
	b = g + pmask + 1;
	for(y = 0, n = 0; y < t->dy; y++) {
		i = y * bytesperline(im->r, t->depth);
		for(x = 0; x < t->dx; x++, n += 3) {
			if(n >= im->chanlen) {
				werrstr("palette overflow");
				return -1;
			}
			pix = (data[i] >> t->depth*((xmask -
				x) & xmask)) & pmask;
			if(((x + 1) & xmask) == 0)
				i++;
			im->chans[0][n] = b[pix];
			im->chans[0][n+1] = g[pix];
			im->chans[0][n+2] = r[pix];
		}
	}
	return 0;
}

static int
parsefield(Tif *t, Fld *f)
{
	uint32_t v;

	v = f->val[0];
	switch(f->tag) {
	case Width:
		t->dx = v;
		break;
	case Length:
		t->dy = v;
		break;
	case Bits:
		t->depth = v;
		if(f->cnt == 3)
			t->depth += f->val[1] + f->val[2];
		break;
	case Compression:
		t->comp = v;
		break;
	case Photometric:
		t->photo = v;
		switch(t->photo) {
		case Whitezero:
		case Blackzero:
			t->decode = greydecode;
			break;
		case Rgb:
			t->decode = rgbdecode;
			break;
		case Palette:
			t->decode = paldecode;
			break;
		default:
			break;
		}
		break;
	case Strips:
		t->strips = f->val;
		t->nstrips = f->cnt;
		break;
	case Fill:
		t->fill = v;
		break;
	case Orientation:
		t->orientation = v;
		break;
	case Samples:
		t->samples = v;
		break;
	case Rows:
		t->rows = v;
		break;
	case Counts:
		t->counts = f->val;
		t->ncounts = f->cnt;
		break;
	case Planar:
		t->planar = v;
		break;
	case T4opts:
		t->t4 = v;
		break;
	case T6opts:
		t->t6 = v;
		break;
	case Predictor:
		t->predictor = v;
		break;
	case Color:
		t->color = f->val;
		t->ncolor = f->cnt;
		break;
	default:
		werrstr("shouldn't reach");
		return -1;
	}
	return 0;
}

static int
readfield(Tif *t, Fld *f)
{
	int size;
	uint32_t i, j, n, off;
	uint32_t (*readval)(Tif *);

	f->tag = readshort(t);
	f->typ = readshort(t);
	f->cnt = readlong(t);
	f->val = nil;
	switch(f->tag) {
	case Width:
	case Length:
	case Compression:
	case Photometric:
	case Fill:
	case Orientation:
	case Samples:
	case Rows:
	case Planar:
	case T4opts:
	case T6opts:
	case Predictor:
		if(f->cnt != 1) {
			werrstr("field count");
			return -1;
		}
		break;
	case Bits:
		if(f->cnt != 1 && f->cnt != 3) {
			werrstr("field count");
			return -1;
		}
		break;
	case Strips:
	case Counts:
	case Color:
		break;
	default:
		readlong(t);
		return 0;
	}
	switch(f->typ) {
	case Byte:
		readval = readbyte;
		break;
	case Short:
		readval = readshort;
		break;
	case Long:
		readval = readlong;
		break;
	default:
		werrstr("unsupported type\n");
		return -1;
	}
	if((f->val = malloc(f->cnt*sizeof *f->val)) == nil)
		return -1;
	size = typesizes[f->typ];
	if((n = size*f->cnt) <= 4) {
		for(i = 0; i < f->cnt; i++)
			f->val[i] = (*readval)(t);
		f->off = 0x0;
		f->nval = i;
		for(j = n; j < 4; j += size)
			(*readval)(t);
	} else {
		f->off = readlong(t);
		off = t->n;
		if(gototif(t, f->off) < 0)
			return -1;
		for(i = 0; i < f->cnt; i++)
			f->val[i] = (*readval)(t);
		f->nval = i;
		if(gototif(t, off) < 0)
			return -1;
	}
	return parsefield(t, f);
}

static int
checkfields(Tif *t)
{
	uint32_t n, size;

	if(t->dx == 0) {
		werrstr("image width");
		return -1;
	}
	if(t->dy == 0) {
		werrstr("image length");
		return -1;
	}
	switch(t->depth) {
	case 1:
	case 4:
	case 8:
	case 24:
		break;
	default:
		werrstr("bits per sample");
		return -1;
	}
	switch(t->comp) {
	case Nocomp:
		t->uncompress = nocomp;
		break;
	case Huffman:
	case T4enc:
	case T6enc:
		t->uncompress = fax;
		if(t->decode != nil)
			t->decode = faxdecode;
		if((t->comp == T4enc && t->t4 & (1<<1)) ||
			(t->comp == T6enc &&
			t->t6 & (1<<1))) {
			werrstr("uncompressed mode");
			return -1;
		}
		break;
	case Lzwenc:
		t->uncompress = lzw;
		break;
	case Packbits:
		t->uncompress = packbits;
		break;
	default:
		werrstr("compression");
		return -1;
	}
	if(t->decode == nil) {
		werrstr("photometric interpretation");
		return -1;
	}
	if(t->depth > 1 && (t->comp == Huffman ||
		t->comp == T4enc || t->comp == T6enc)) {
		werrstr("compression");
		return -1;
	}
	if(t->fill != 1 && t->fill != 2) {
		werrstr("fill order");
		return -1;
	}
	if(t->fill == 2 && t->depth != 1) {
		werrstr("depth should be 1 with fill order 2");
		return -1;
	}
	if(t->orientation != 1) {
		werrstr("orientation");
		return -1;
	}
	if(t->rows == 0) {
		werrstr("rows per strip");
		return -1;
	}
	n = (t->dy + t->rows - 1) / t->rows;
	if(t->strips == nil || t->nstrips != n) {
		werrstr("strip offsets");
		return -1;
	}
	if(t->samples == 1 && t->photo == Rgb) {
		werrstr("not enough samples per pixel");
		return -1;
	}
	if(t->samples == 3 && t->photo != Rgb) {
		werrstr("too many samples per pixel");
		return -1;
	}
	if(t->samples != 1 && t->samples != 3) {
		werrstr("samples per pixel");
		return -1;
	}
	/*
	* strip byte counts should not be missing,
	* but we can guess correctly in this case
	*/
	size = sizeof *t->counts;
	if(t->counts == nil && t->comp == Nocomp &&
		t->nstrips == 1 &&
		(t->counts = malloc(size)) != nil) {
		t->counts[0] = (t->dx*t->dy*t->depth + 7) / 8;
		t->ncounts = t->nstrips;
	}
	if(t->counts == nil || t->ncounts != t->nstrips) {
		werrstr("strip byte counts");
		return -1;
	}
	if(t->planar != 1) {
		werrstr("planar configuration");
		return -1;
	}
	if(t->photo == Palette && (t->color == nil ||
		t->ncolor != 3*(1<<t->depth))) {
		werrstr("color map");
		return -1;
	}
	return 0;
}

static int
readstrips(Tif *t)
{
	int i, j, n;
	uint32_t off;

	t->data = nil;
	t->ndata = 0;
	for(i = 0; i < t->nstrips; i++)
		t->ndata += t->counts[i];
	if(t->ndata == 0) {
		werrstr("no image data");
		return -1;
	}
	if((t->data = malloc(t->ndata*sizeof *t->data)) == nil)
		return -1;
	off = t->n;
	for(i = n = 0; i < t->nstrips; i++) {
		if(gototif(t, t->strips[i]) < 0)
			return -1;
		/*
		* we store each strip's offset in t->data
		* in order to skip the final rtc or eofb
		* during fax decoding. t->strips is used
		* to save on memory allocation. these
		* offsets are also used in lzw as a
		* preventive measure.
		*/
		t->strips[i] = n;
		for(j = 0; j < t->counts[i]; j++, n++)
			t->data[n] = readbyte(t);
	}
	return gototif(t, off);
}

static Rawimage *
decode(Tif *t)
{
	uint32_t size;
	unsigned char *data;
	Rawimage *im;

	if((im = malloc(sizeof *im)) == nil)
		return nil;
	im->r = Rect(0, 0, t->dx, t->dy);
	im->cmap = nil;
	im->cmaplen = 0;
	im->chanlen = t->dx * t->dy;
	if(t->photo == Rgb || t->photo == Palette) {
		im->chandesc = CRGB24;
		im->chanlen *= 3;
	} else
		im->chandesc = CY;
	im->nchans = 1;
	size = im->chanlen * sizeof *im->chans[0];
	if((im->chans[0] = malloc(size)) == nil)
		return nil;
	/* unused members */
	im->fields = 0;
	im->gifflags = 0;
	im->gifdelay = 0;
	im->giftrindex = 0;
	im->gifloopcount = 1;
	if((data = (*t->uncompress)(t)) == nil)
		return nil;
	if((*t->decode)(t, im, data) < 0) {
		free(im->chans[0]);
		free(im);
		im = nil;
	}
	free(data);
	return im;
}

static void
freefields(Tif *t)
{
	uint i;

	for(i = 0; i < t->nfld; i++) {
		if(t->fld[i].val != nil)
			free(t->fld[i].val);
	}
	free(t->fld);
}

static Rawimage *
readslave(Tif *t)
{
	uint i, j;
	Rawimage *r;

	if(readheader(t) < 0)
		return nil;
	if((t->nfld = readshort(t)) <= 0) {
		werrstr("illegal field number: %#.4x", t->nfld);
		return nil;
	}
	if((t->fld = malloc(t->nfld*sizeof *t->fld)) == nil)
		return nil;
	for(i = 0; i < t->nfld; i++) {
		if(readfield(t, &t->fld[i]) < 0) {
			if(t->fld[i].val != nil)
				free(t->fld[i].val);
			break;
		}
	}
	if(i < t->nfld) {
		for(j = 0; j < i; j++) {
			if(t->fld[j].val != nil)
				free(t->fld[j].val);
		}
		free(t->fld);
		return nil;
	}
	readlong(t);
	if(checkfields(t) < 0) {
		freefields(t);
		return nil;
	}
	if(readstrips(t) < 0) {
		freefields(t);
		if(t->data != nil)
			free(t->data);
		return nil;
	}
	free(t->buf);
	r = decode(t);
	freefields(t);
	return r;
}

Rawimage **
Breadtif(Biobuf *b, int colorspace)
{
	Rawimage **array, *r;
	Tif *t;

	if(colorspace != CRGB24) {
		werrstr("unknown color space: %d",
			colorspace);
		return nil;
	}
	if((t = malloc(sizeof *t)) == nil)
		return nil;
	if((array = malloc(2*sizeof *array)) == nil)
		return nil;
	t->fd = b;
	t->buf = nil;
	t->nbuf = t->eof = t->n = 0;
	/* order doesn't matter for the first two bytes */
	t->byte2 = byte2le;
	t->byte4 = byte4le;
	/* defaults */
	t->dx = 0;
	t->dy = 0;
	t->depth = 1;
	t->comp = 1;
	t->uncompress = nil;
	t->photo = 0;
	t->decode = nil;
	t->fill = 1;
	t->orientation = 1;
	t->strips = nil;
	t->nstrips = 0;
	t->samples = 1;
	t->rows = 0xffffffff; /* entire image is one strip */
	t->counts = nil;
	t->ncounts = 0;
	t->planar = 1;
	t->t4 = 0;
	t->t6 = 0;
	t->predictor = 1;
	t->color = nil;
	t->ncolor = 0;
	r = readslave(t);
	free(t);
	array[0] = r;
	array[1] = nil;
	return array;
}

Rawimage **
readtif(int fd, int colorspace)
{
	Rawimage **a;
	Biobuf b;

	if(Binit(&b, fd, OREAD) < 0)
		return nil;
	a = Breadtif(&b, colorspace);
	Bterm(&b);
	return a;
}
