#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>

void
usage(void)
{
	fprint(2, "auth/asn12dsa [-t tag] [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s;
	uint8_t *buf;
	int fd;
	int32_t n, tot;
	char *tag;
	DSApriv *key;

	fmtinstall('B', mpfmt);

	tag = nil;
	ARGBEGIN{
	case 't':
		tag = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0 && argc != 1)
		usage();

	fd = 0;
	if(argc == 1){
		if((fd = open(*argv, OREAD)) < 0)
			sysfatal("open %s: %r", *argv);
	}

	buf = nil;
	tot = 0;
	for(;;){
		buf = realloc(buf, tot+8192);
		if(buf == nil)
			sysfatal("realloc: %r");
		if((n = read(fd, buf+tot, 8192)) < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		tot += n;
	}

	key = asn1toDSApriv(buf, tot);
	if(key == nil)
		sysfatal("couldn't parse asn1 key");

	s = smprint("key proto=dsa %s%sp=%B q=%B alpha=%B key=%B !secret=%B\n",
		tag ? tag : "", tag ? " " : "",
		key->pub.p, key->pub.q, key->pub.alpha, key->pub.key,
		key->secret);
	if(s == nil)
		sysfatal("smprint: %r");
	write(1, s, strlen(s));
	exits(0);
}
