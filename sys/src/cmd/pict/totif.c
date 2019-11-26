#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include "imagefile.h"

static Memimage *memtochan(Memimage *, uint32_t);

void
usage(void)
{
	fprint(2, "usage: %s [-c 'comment'] "
		"[-3bgGhklLptvyY] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Biobuf bout;
	Memimage *i, *ni;
	int fd, chanflag, comp, opt;
	char *err, *file, *c;
	uint32_t chan;

	chan = BGR24;
	chanflag = opt = 0;
	comp = 1;
	c = nil;
	ARGBEGIN {
	case '3': /* force RGB */
		chan = BGR24;
		chanflag = 1;
		break;
	case 'b':
		chan = GREY1;
		chanflag = 1;
		break;
	case 'c':
		c = EARGF(usage());
		break;
	case 'g': /* t4 */
		comp = 3;
		opt = 0;
		break;
	case 'G': /* t4 two-dimensional */
		comp = 3;
		opt = 1;
		break;
	case 'h': /* huffman */
		comp = 2;
		break;
	case 'k':
		chan = GREY8;
		chanflag = 1;
		break;
	case 'l': /* lzw */
		comp = 5;
		opt = 0;
		break;
	case 'L': /* lzw, horizontal differencing */
		comp = 5;
		opt = 1;
		break;
	case 'p': /* packbits */
		comp = 0x8005;
		break;
	case 't': /* t6 */
		comp = 4;
		break;
	case 'v': /* RGBV */
		chan = CMAP8;
		chanflag = 1;
		break;
	case 'y':
		chan = GREY2;
		chanflag = 1;
		break;
	case 'Y':
		chan = GREY4;
		chanflag = 1;
		break;
	default:
		usage();
	} ARGEND
	if(argc > 1)
		usage();
	if(argc == 0) {
		file = "<stdin>";
		fd = 0;
	} else {
		file = argv[0];
		if((fd = sys_open(file, OREAD)) < 0)
			sysfatal("open %s: %r", file);
	}
	if(Binit(&bout, 1, OWRITE) < 0)
		sysfatal("Binit: %r");
	memimageinit();
	if((i = readmemimage(fd)) == nil)
		sysfatal("readmemimage %s: %r", file);
	sys_close(fd);
	if(comp >= 2 && comp <= 4) {
		chan = GREY1;
		chanflag = 1;
	} else if(chan == GREY2) {
		if((ni = memtochan(i, chan)) == nil)
			sysfatal("memtochan: %r");
		if(i != ni) {
			freememimage(i);
			i = ni;
		}
		chan = GREY4;
	}
	if(!chanflag) {
		switch(i->chan) {
		case GREY1:
		case GREY4:
		case GREY8:
		case CMAP8:
		case BGR24:
			break;
		case GREY2:
			chan = GREY4;
			chanflag = 1;
			break;
		default:
			chanflag = 1;
			break;
		}
	}
	if(chanflag) {
		if((ni = memtochan(i, chan)) == nil)
			sysfatal("memtochan: %r");
		if(i != ni) {
			freememimage(i);
			i = ni;
		}
	}
	if((err = memwritetif(&bout, i, c, comp, opt)) != nil)
		fprint(2, "%s: %s\n", argv0, err);
	freememimage(i);
	exits(err);
}

static Memimage *
memtochan(Memimage *i, uint32_t chan)
{
	Memimage *ni;

	if(i->chan == chan)
		return i;
	if((ni = allocmemimage(i->r, chan)) == nil)
		return nil;
	memimagedraw(ni, ni->r, i, i->r.min, nil, i->r.min, S);
	return ni;
}
