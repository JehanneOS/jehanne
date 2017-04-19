#include <u.h>
#include <lib9.h>

#define INT	2
#define FLOAT	2.5
#define A	4	// addition result
#define M	5	// multiplication result

void
main()
{
	int a, b, x, y;
	float f;
	double d;

	f = FLOAT;
	d = FLOAT;
	a = b = x = y = INT;

	a += (double)d;
	b *= (double)d;
	x += (float)f;
	y *= (float)f;

	fprint(2, "[double] addition: %d; multiplication: %d\n", a, b);

	fprint(2, "[float] addition: %d; multiplication: %d\n", x, y);

	if(a != A || x != A || b != M || y != M)
		exits("FAIL");
	print("PASS\n");
	exits("PASS");
}
