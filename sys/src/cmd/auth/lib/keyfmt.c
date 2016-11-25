#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

/*
 * print a key in des standard form
 */
int
deskeyfmt(Fmt *f)
{
	uint8_t key[8];
	char buf[32];
	uint8_t *k;
	int i;

	k = va_arg(f->args, uint8_t*);
	key[0] = 0;
	for(i = 0; i < 7; i++){
		key[i] |= k[i] >> i;
		key[i] &= ~1;
		key[i+1] = k[i] << (7 - i);
	}
	key[7] &= ~1;
	sprint(buf, "%.3uo %.3uo %.3uo %.3uo %.3uo %.3uo %.3uo %.3uo",
		key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
	fmtstrcpy(f, buf);
	return 0;
}
