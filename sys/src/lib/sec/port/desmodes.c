#include "os.h"
#include <libsec.h>

/*
 *  these routines use the 64bit format for
 *  DES keys.
 */

void
setupDESstate(DESstate *s, uint8_t key[8], uint8_t *ivec)
{
	jehanne_memset(s, 0, sizeof(*s));
	jehanne_memmove(s->key, key, sizeof(s->key));
	des_key_setup(key, s->expanded);
	if(ivec)
		jehanne_memmove(s->ivec, ivec, 8);
	s->setup = 0xdeadbeef;
}

void
setupDES3state(DES3state *s, uint8_t key[3][8], uint8_t *ivec)
{
	jehanne_memset(s, 0, sizeof(*s));
	jehanne_memmove(s->key, key, sizeof(s->key));
	des_key_setup(key[0], s->expanded[0]);
	des_key_setup(key[1], s->expanded[1]);
	des_key_setup(key[2], s->expanded[2]);
	if(ivec)
		jehanne_memmove(s->ivec, ivec, 8);
	s->setup = 0xdeadbeef;
}
