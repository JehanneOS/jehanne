#include "os.h"
#include <mp.h>
#include <libsec.h>

uint8_t key[] = "Jefe";
uint8_t data[] = "what do ya want for nothing?";

void
main(void)
{
	int i;
	uint8_t hash[MD5dlen];

	hmac_md5(data, jehanne_strlen((char*)data), key, 4, hash, nil);
	for(i=0; i<MD5dlen; i++)
		jehanne_print("%2.2x", hash[i]);
	jehanne_print("\n");
	jehanne_print("750c783e6ab0b503eaa86e310a5db738\n");
}
