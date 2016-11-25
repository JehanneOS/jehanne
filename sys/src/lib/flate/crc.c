#include <u.h>
#include <libc.h>
#include <flate.h>

uint32_t*
mkcrctab(uint32_t poly)
{
	uint32_t *crctab;
	uint32_t crc;
	int i, j;

	crctab = malloc(256 * sizeof(uint32_t));
	if(crctab == nil)
		return nil;

	for(i = 0; i < 256; i++){
		crc = i;
		for(j = 0; j < 8; j++){
			if(crc & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc >>= 1;
		}
		crctab[i] = crc;
	}
	return crctab;
}

uint32_t
blockcrc(uint32_t *crctab, uint32_t crc, void *vbuf, int n)
{
	uint8_t *buf, *ebuf;

	crc ^= 0xffffffff;
	buf = vbuf;
	ebuf = buf + n;
	while(buf < ebuf)
		crc = crctab[(crc & 0xff) ^ *buf++] ^ (crc >> 8);
	return crc ^ 0xffffffff;
}
