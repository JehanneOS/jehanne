#include <u.h>
#include <lib9.h>
#include "libsec.h"

char *tests[] = {
	"",
	"a",
	"abc",
	"message digest",
	"abcdefghijklmnopqrstuvwxyz",
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
	"123456789012345678901234567890123456789012345678901234567890"
		"12345678901234567890",
	"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
	"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhi"
		"jklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
	0
};

void
main(void)
{
	int i;
	char **pp;
	uint8_t *p;
	uint8_t digest[SHA2_512dlen];

	jehanne_print("SHA2_224 tests:\n");
	for(pp = tests; *pp; pp++){
		p = (uint8_t*)*pp;
		sha2_224(p, jehanne_strlen(*pp), digest, 0);
		for(i = 0; i < SHA2_224dlen; i++)
			jehanne_print("%2.2ux", digest[i]);
		jehanne_print("\n");
	}

	jehanne_print("\nSHA256 tests:\n");
	for(pp = tests; *pp; pp++){
		p = (uint8_t*)*pp;
		sha2_256(p, jehanne_strlen(*pp), digest, 0);
		for(i = 0; i < SHA2_256dlen; i++)
			jehanne_print("%2.2ux", digest[i]);
		jehanne_print("\n");
	}

	jehanne_print("\nSHA384 tests:\n");
	for(pp = tests; *pp; pp++){
		p = (uint8_t*)*pp;
		sha2_384(p, jehanne_strlen(*pp), digest, 0);
		for(i = 0; i < SHA2_384dlen; i++)
			jehanne_print("%2.2ux", digest[i]);
		jehanne_print("\n");
	}

	jehanne_print("\nSHA512 tests:\n");
	for(pp = tests; *pp; pp++){
		p = (uint8_t*)*pp;
		sha2_512(p, jehanne_strlen(*pp), digest, 0);
		for(i = 0; i < SHA2_512dlen; i++)
			jehanne_print("%2.2ux", digest[i]);
		jehanne_print("\n");
	}
}
