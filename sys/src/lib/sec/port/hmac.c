#include "os.h"
#include <libsec.h>

/* rfc2104 */
DigestState*
hmac_x(uint8_t *p, uint32_t len, uint8_t *key, uint32_t klen, uint8_t *digest, DigestState *s,
	DigestState*(*x)(uint8_t*, uint32_t, uint8_t*, DigestState*), int xlen)
{
	int i;
	uint8_t pad[Hmacblksz+1], innerdigest[256];

	if(xlen > sizeof(innerdigest))
		return nil;
	if(klen > Hmacblksz){
		if(xlen > Hmacblksz)
			return nil;
		(*x)(key, klen, innerdigest, nil);
		key = innerdigest;
		klen = xlen;
	}

	/* first time through */
	if(s == nil || s->seeded == 0){
		memset(pad, 0x36, Hmacblksz);
		pad[Hmacblksz] = 0;
		for(i = 0; i < klen; i++)
			pad[i] ^= key[i];
		s = (*x)(pad, Hmacblksz, nil, s);
		if(s == nil)
			return nil;
	}

	s = (*x)(p, len, nil, s);
	if(digest == nil)
		return s;

	/* last time through */
	memset(pad, 0x5c, Hmacblksz);
	pad[Hmacblksz] = 0;
	for(i = 0; i < klen; i++)
		pad[i] ^= key[i];
	(*x)(nil, 0, innerdigest, s);
	s = (*x)(pad, Hmacblksz, nil, nil);
	(*x)(innerdigest, xlen, digest, s);
	return nil;
}
