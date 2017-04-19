#include "os.h"
#include <mp.h>
#include <libsec.h>

static uint8_t nine[32] = {9};

void
curve25519_dh_new(uint8_t x[32], uint8_t y[32])
{
	uint8_t b;

	/* new public/private key pair */
	genrandom(x, 32);
	b = x[31];
	x[0] &= ~7;			/* clear bit 0,1,2 */
	x[31] = 0x40 | (b & 0x7f);	/* set bit 254, clear bit 255 */
	curve25519(y, x, nine);

	/* bit 255 is always 0, so make it random */
	y[31] |= b & 0x80;
}

void
curve25519_dh_finish(uint8_t x[32], uint8_t y[32], uint8_t z[32])
{
	/* remove the random bit */
	y[31] &= 0x7f;

	/* calculate dhx key */
	curve25519(z, x, y);

	jehanne_memset(x, 0, 32);
	jehanne_memset(y, 0, 32);
}
