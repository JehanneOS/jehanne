#include <u.h>
#include <lib9.h>
#include <mp.h>
#include <libsec.h>

typedef struct Test Test;
struct Test
{
	char *K;
	char *P;
	char *A;
	char *IV;
	char *T;
};

Test tests[] = {
	{	/* Test Case 1 */
		"00000000000000000000000000000000",
		"",
		"",
		"000000000000000000000000",

		"58E2FCCEFA7E3061367F1D57A4E7455A"
	},
	{	/* Test Case 2 */
		"00000000000000000000000000000000",
		"00000000000000000000000000000000",
		"",
		"000000000000000000000000",

		"AB6E47D42CEC13BDF53A67B21257BDDF",
	},
	{	/* Test Case 3 */
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b391aafd255",
		"",
		"cafebabefacedbaddecaf888",

		"4D5C2AF327CD64A62CF35ABD2BA6FAB4"
	},
	{	/* Test Case 4 */
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbaddecaf888",

		"5BC94FBC3221A5DB94FAE95AE7121A47"
	},
	{	/* Test Case 5 */
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbad",

		"3612D2E79E3B0785561BE14AACA2FCCB"
	},
	{	/* Test Case 6 */
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"9313225df88406e555909c5aff5269aa"
		"6a7a9538534f7da1e4c303d2a318a728"
		"c3c0c95156809539fcf0e2429a6b5254"
		"16aedbf5a0de6a57a637b39b",

		"619CC5AEFFFE0BFA462AF43C1699D050"
	},
	{	/* Test Case 7 */
		"00000000000000000000000000000000"
		"0000000000000000",
		"",
		"",
		"000000000000000000000000",

		"CD33B28AC773F74BA00ED1F312572435"
	},
	{	/* Test Case 8 */
		"00000000000000000000000000000000"
		"0000000000000000",
		"00000000000000000000000000000000",
		"",
		"000000000000000000000000",

		"2FF58D80033927AB8EF4D4587514F0FB"
	},
	{	/* Test Case 9 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b391aafd255",
		"",
		"cafebabefacedbaddecaf888",

		"9924A7C8587336BFB118024DB8674A14"
	},
	{	/* Test Case 10 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbaddecaf888",

		"2519498E80F1478F37BA55BD6D27618C"
	},
	{	/* Test Case 11 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbad",

		"65DCC57FCF623A24094FCCA40D3533F8"
	},
	{	/* Test Case 12 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"9313225df88406e555909c5aff5269aa"
		"6a7a9538534f7da1e4c303d2a318a728"
		"c3c0c95156809539fcf0e2429a6b5254"
		"16aedbf5a0de6a57a637b39b",

		"DCF566FF291C25BBB8568FC3D376A6D9"
	},
	{	/* Test Case 13 */
		"00000000000000000000000000000000"
		"00000000000000000000000000000000",
		"",
		"",
		"000000000000000000000000",

		"530F8AFBC74536B9A963B4F1C4CB738B"
	},
	{	/* Test Case 14 */
		"00000000000000000000000000000000"
		"00000000000000000000000000000000",
		"00000000000000000000000000000000",
		"",
		"000000000000000000000000",

		"D0D1C8A799996BF0265B98B5D48AB919"
	},
	{	/* Test Case 15 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b391aafd255",
		"",
		"cafebabefacedbaddecaf888",

		"B094DAC5D93471BDEC1A502270E3CC6C"
	},
	{	/* Test Case 16 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbaddecaf888",

		"76FC6ECE0F4E1768CDDF8853BB2D551B"
	},
	{	/* Test Case 17 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"cafebabefacedbad",

		"3A337DBF46A792C45E454913FE2EA8F2"
	},
	{	/* Test Case 18 */
		"feffe9928665731c6d6a8f9467308308"
		"feffe9928665731c6d6a8f9467308308",
		"d9313225f88406e5a55909c5aff5269a"
		"86a7a9531534f7da2e4c303d8a318a72"
		"1c3c0c95956809532fcf0e2449a6b525"
		"b16aedf5aa0de657ba637b39",
		"feedfacedeadbeeffeedfacedeadbeef"
		"abaddad2",
		"9313225df88406e555909c5aff5269aa"
		"6a7a9538534f7da1e4c303d2a318a728"
		"c3c0c95156809539fcf0e2429a6b5254"
		"16aedbf5a0de6a57a637b39b",

		"A44A8266EE1C8EB0C8B5D4CF5AE9F19A"
	},
};

int
parsehex(char *s, uint8_t *h, char *l)
{
	char *e;
	mpint *m;
	int n;

	n = jehanne_strlen(s);
	if(n == 0)
		return 0;
	assert((n & 1) == 0);
	n >>= 1;
	e = nil;
	m = strtomp(s, &e, 16, nil);
	if(m == nil || *e != '\0')
		abort();
	mptober(m, h, n);
	if(l != nil)
		jehanne_print("%s = %.*H\n", l, n, h);
	return n;
}

void
runtest(Test *t)
{
	AESGCMstate s;
	uint8_t key[1024], plain[1024], aad[1024], iv[1024], tag[16], tmp[16];
	int nkey, nplain, naad, niv;

	nkey = parsehex(t->K, key, "K");
	nplain = parsehex(t->P, plain, "P");
	naad = parsehex(t->A, aad, "A");
	niv = parsehex(t->IV, iv, "IV");

	setupAESGCMstate(&s, key, nkey, iv, niv);
	aesgcm_encrypt(plain, nplain, aad, naad, tag, &s);
	jehanne_print("C = %.*H\n", nplain, plain);
	jehanne_print("T = %.*H\n", 16, tag);

	parsehex(t->T, tmp, nil);
	assert(jehanne_memcmp(tmp, tag, 16) == 0);
}

void
perftest(void)
{
	AESGCMstate s;
	static uint8_t zeros[16];
	uint8_t buf[1024*1024], tag[16];
	int64_t now;
	int i, delta;

	now = jehanne_nsec();
	for(i=0; i<100; i++){
		jehanne_memset(buf, 0, sizeof(buf));
		if(1){
			setupAESGCMstate(&s, zeros, 16, zeros, 12);
			aesgcm_encrypt(buf, sizeof(buf), nil, 0, tag, &s);
		} else {
			setupAESstate(&s, zeros, 16, zeros);
			aesCBCencrypt(buf, sizeof(buf), &s);
		}
	}
	delta = (jehanne_nsec() - now) / 1000000000LL;
	jehanne_fprint(2, "%ds = %d/s\n", delta, i*sizeof(buf) / delta);
}

void
main(int argc, char **argv)
{
	int i;

	jehanne_fmtinstall('H', encodefmt);

	ARGBEGIN {
	case 'p':
		perftest();
		jehanne_exits(nil);
	} ARGEND;

	for(i=0; i<nelem(tests); i++){
		jehanne_print("Test Case %d\n", i+1);
		runtest(&tests[i]);
		jehanne_print("\n");
	}
}
