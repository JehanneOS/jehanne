#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

void
hname(char *buf)
{
	uint8_t d[SHA1dlen];
	uint32_t x;
	int n;

	n = strlen(buf);
	sha1((uint8_t*)buf, n, d, nil);
	x = d[0] | d[1]<<8 | d[2]<<16;
	snprint(buf, n+1, "%.5ux", x & 0xfffff);
}
