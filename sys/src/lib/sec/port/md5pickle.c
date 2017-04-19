#include "os.h"
#include <libsec.h>

char*
md5pickle(MD5state *s)
{
	char *p;
	int m, n;

	m = 17+4*9+4*((s->blen+3)/3 + 1);
	p = jehanne_malloc(m);
	if(p == nil)
		return p;
	n = jehanne_sprint(p, "%16.16llux %8.8ux %8.8ux %8.8ux %8.8ux ",
		s->len,
		s->state[0], s->state[1], s->state[2],
		s->state[3]);
	jehanne_enc64(p+n, m-n, s->buf, s->blen);
	return p;
}

MD5state*
md5unpickle(char *p)
{
	MD5state *s;

	s = jehanne_malloc(sizeof(*s));
	if(s == nil)
		return nil;
	s->len = jehanne_strtoull(p, &p, 16);
	s->state[0] = jehanne_strtoul(p, &p, 16);
	s->state[1] = jehanne_strtoul(p, &p, 16);
	s->state[2] = jehanne_strtoul(p, &p, 16);
	s->state[3] = jehanne_strtoul(p, &p, 16);
	s->blen = jehanne_dec64(s->buf, sizeof(s->buf), p, jehanne_strlen(p));
	s->malloced = 1;
	s->seeded = 1;
	return s;
}
