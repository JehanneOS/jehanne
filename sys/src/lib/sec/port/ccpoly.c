#include "os.h"
#include <libsec.h>

static void
ccpolyotk(Chachastate *cs, DigestState *ds)
{
	uint8_t otk[ChachaBsize];

	jehanne_memset(ds, 0, sizeof(*ds));
	jehanne_memset(otk, 0, 32);
	chacha_setblock(cs, 0);
	chacha_encrypt(otk, ChachaBsize, cs);
	poly1305(nil, 0, otk, 32, nil, ds);
}

static void
ccpolypad(uint8_t *buf, uint32_t nbuf, DigestState *ds)
{
	static uint8_t zeros[16] = {0};
	uint32_t npad;

	if(nbuf == 0)
		return;
	poly1305(buf, nbuf, nil, 0, nil, ds);
	npad = nbuf % 16;
	if(npad == 0)
		return;
	poly1305(zeros, 16 - npad, nil, 0, nil, ds);
}

static void
ccpolylen(uint32_t n, uint8_t tag[16], DigestState *ds)
{
	uint8_t info[8];

	info[0] = n;
	info[1] = n>>8;
	info[2] = n>>16;
	info[3] = n>>24;
	info[4] = 0;
	info[5] = 0;
	info[6] = 0;
	info[7] = 0;
	poly1305(info, 8, nil, 0, tag, ds);
}

void
ccpoly_encrypt(uint8_t *dat, uint32_t ndat, uint8_t *aad, uint32_t naad, uint8_t tag[16], Chachastate *cs)
{
	DigestState ds;

	ccpolyotk(cs, &ds);
	ds.malloced = 0;
	if(cs->ivwords == 2){
		poly1305(aad, naad, nil, 0, nil, &ds);
		ccpolylen(naad, nil, &ds);
		chacha_encrypt(dat, ndat, cs);
		poly1305(dat, ndat, nil, 0, nil, &ds);
		ccpolylen(ndat, tag, &ds);
	} else {
		ccpolypad(aad, naad, &ds);
		chacha_encrypt(dat, ndat, cs);
		ccpolypad(dat, ndat, &ds);
		ccpolylen(naad, nil, &ds);
		ccpolylen(ndat, tag, &ds);
	}
}

int
ccpoly_decrypt(uint8_t *dat, uint32_t ndat, uint8_t *aad, uint32_t naad, uint8_t tag[16], Chachastate *cs)
{
	DigestState ds;
	uint8_t tmp[16];

	ccpolyotk(cs, &ds);
	ds.malloced = 0;
	if(cs->ivwords == 2){
		poly1305(aad, naad, nil, 0, nil, &ds);
		ccpolylen(naad, nil, &ds);
		poly1305(dat, ndat, nil, 0, nil, &ds);
		ccpolylen(ndat, tmp, &ds);
	} else {
		ccpolypad(aad, naad, &ds);
		ccpolypad(dat, ndat, &ds);
		ccpolylen(naad, nil, &ds);
		ccpolylen(ndat, tmp, &ds);
	}
	if(tsmemcmp(tag, tmp, 16) != 0)
		return -1;
	chacha_encrypt(dat, ndat, cs);
	return 0;
}
