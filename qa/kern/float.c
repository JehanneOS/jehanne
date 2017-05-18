#include <u.h>
#include <lib9.h>

#define DPRECSTR	"0.0000004000000000125"
#define DPREC	0.0000004000000000125
#define DIEEELO	0x9ac0499f
#define DIEEEHI	0x3e9ad7f2

jmp_buf errj;
char *err;

void
catcher(void *u, char *s)
{
	err = 0;
	if(strncmp(s, "sys: fp:", 8) == 0){
		err = s;
		notejmp(u, errj, 0);
	}
	noted(NDFLT);
}

void
tstdiv(double p)
{
	double r = 1.0;

	r /= p;
	fprint(2, "1/%0.20g = %0.20g\n", p, r);
} 

typedef union I2UL
{
	int d[2];
	unsigned long l;
}I2UL;
typedef union D2UL
{
	double d;
	unsigned long l;
}D2UL;

void
main(void)
{
	double p = DPREC;
	int d[2] = { DIEEELO, DIEEEHI };
	I2UL i2l;
	D2UL d2l;
	uint64_t dieee, q;

	i2l.d[0] = d[0];
	i2l.d[1] = d[1];
	dieee = i2l.l;
	d2l.d = p;
	q = d2l.l;

	err = 0;
	notify(catcher);
	setjmp(errj);
	if(err){
		fprint(2, "FAIL: %s\n", err);
		exits("FAIL");
	}

	fprint(2, "Double-precision test number: %s\n", DPRECSTR);
	fprint(2, "Expected internal representation: %ullx\n", dieee);
	fprint(2, "Actual internal representation: %ullx\n", q);

	if(q != dieee) {
		print("FAIL\n");
		exits("FAIL");
	}

	tstdiv(p);
	print("PASS\n");
	exits("PASS");
}
