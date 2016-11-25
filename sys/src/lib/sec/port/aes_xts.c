// Author Taru Karttunen <taruti@taruti.net>
// This file can be used as both Public Domain or Creative Commons CC0.
#include "os.h"
#include <libsec.h>

#define AesBlockSize 16

static void xor128(uint8_t* o,uint8_t* i1,uint8_t* i2) {
	((uint32_t*)o)[0] = ((uint32_t*)i1)[0] ^ ((uint32_t*)i2)[0];
	((uint32_t*)o)[1] = ((uint32_t*)i1)[1] ^ ((uint32_t*)i2)[1];
	((uint32_t*)o)[2] = ((uint32_t*)i1)[2] ^ ((uint32_t*)i2)[2];
	((uint32_t*)o)[3] = ((uint32_t*)i1)[3] ^ ((uint32_t*)i2)[3];
}

static void gf_mulx(uint8_t* x) {
    uint32_t t = ((((uint32_t*)(x))[3] & 0x80000000u) ? 0x00000087u : 0);;
    ((uint32_t*)(x))[3] = (((uint32_t*)(x))[3] << 1) | (((uint32_t*)(x))[2] & 0x80000000u ? 1 : 0);
    ((uint32_t*)(x))[2] = (((uint32_t*)(x))[2] << 1) | (((uint32_t*)(x))[1] & 0x80000000u ? 1 : 0);
    ((uint32_t*)(x))[1] = (((uint32_t*)(x))[1] << 1) | (((uint32_t*)(x))[0] & 0x80000000u ? 1 : 0);
    ((uint32_t*)(x))[0] = (((uint32_t*)(x))[0] << 1) ^ t;

}

int aes_xts_encrypt(uint32_t tweak[], uint32_t ecb[],  int64_t sectorNumber, uint8_t *input, uint8_t *output, uint32_t len) {
	uint8_t T[16], x[16];
	int i;
	
	if(len % 16 != 0)
		return -1;

	for(i=0; i<AesBlockSize; i++) {
		T[i] = (uint8_t)(sectorNumber & 0xFF);
		sectorNumber = sectorNumber >> 8;
	}
	
	aes_encrypt(tweak, 10, T, T);

	for (i=0; i<len; i+=AesBlockSize) {
		xor128(&x[0], &input[i], &T[0]);
		aes_encrypt(ecb, 10, x, x);
		xor128(&output[i], &x[0], &T[0]);
		gf_mulx(&T[0]);
	}
	return 0;
}

int aes_xts_decrypt(uint32_t tweak[], uint32_t ecb[], int64_t sectorNumber, uint8_t *input, uint8_t *output, uint32_t len) {
	uint8_t T[16], x[16];
	int i;
	
	if(len % 16 != 0)
		return -1;

	for(i=0; i<AesBlockSize; i++) {
		T[i] = (uint8_t)(sectorNumber & 0xFF);
		sectorNumber = sectorNumber >> 8;
	}
	
	aes_encrypt(tweak, 10, T, T);

	for (i=0; i<len; i+=AesBlockSize) {
		xor128(&x[0], &input[i], &T[0]);
		aes_decrypt(ecb, 10, x, x);
		xor128(&output[i], &x[0], &T[0]);
		gf_mulx(&T[0]);
	}
	return 0;
}

