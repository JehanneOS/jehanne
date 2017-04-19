#include <u.h>
#include <lib9.h>
#include <authsrv.h>
#include <libsec.h>

void
passtodeskey(char key[DESKEYLEN], char *p)
{
	uint8_t buf[ANAMELEN], *t;
	int i, n;

	n = strlen(p);
	if(n >= ANAMELEN)
		n = ANAMELEN-1;
	memset(buf, ' ', 8);
	t = buf;
	strncpy((char*)t, p, n);
	t[n] = 0;
	memset(key, 0, DESKEYLEN);
	for(;;){
		for(i = 0; i < DESKEYLEN; i++)
			key[i] = (t[i] >> i) + (t[i+1] << (8 - (i+1)));
		if(n <= 8)
			return;
		n -= 8;
		t += 8;
		if(n < 8){
			t -= 8 - n;
			n = 8;
		}
		encrypt(key, t, 8);
	}
}

void
passtoaeskey(uint8_t key[AESKEYLEN], char *p)
{
	static char salt[] = "Plan 9 key derivation";
	pbkdf2_x((uint8_t*)p, strlen(p), (uint8_t*)salt, sizeof(salt)-1, 9001, key, AESKEYLEN, hmac_sha1, SHA1dlen);
}

void
passtokey(Authkey *key, char *pw)
{
	memset(key, 0, sizeof(Authkey));
	passtodeskey(key->des, pw);
	passtoaeskey(key->aes, pw);
}
