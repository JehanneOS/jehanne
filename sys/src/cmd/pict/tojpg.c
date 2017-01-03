#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <ctype.h>
#include <bio.h>
#include "imagefile.h"

void
usage(void)
{
	fprint(2, "usage: %s [-c 'comment'] [-ks] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Biobuf bout;
	Memimage *i, *ni;
	int fd, kflag, sflag;
	char *err, *file, *com;

	kflag = sflag = 0;
	com = nil;
	ARGBEGIN {
	case 'c':
		com = EARGF(usage());
		break;
	case 'k':
		kflag = 1;
		break;
	case 's':
		sflag = 1;
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
		if((fd = open(file, OREAD)) < 0)
			sysfatal("open %s: %r", file);
	}

	if(Binit(&bout, 1, OWRITE) < 0)
		sysfatal("Binit: %r");
	memimageinit();

	if((i = readmemimage(fd)) == nil)
		sysfatal("readimage %s: %r", file);
	close(fd);
	if((ni = memmultichan(i)) == nil)
		sysfatal("converting image to RGB24: %r");
	if(i != ni) {
		freememimage(i);
		i = ni;
	}
	err = memwritejpg(&bout, i, com, kflag, sflag);
	freememimage(i);

	if(err != nil)
		fprint(2, "%s: %s\n", argv0, err);
	exits(err);
}
