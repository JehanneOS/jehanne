#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
bye(void)
{
	printf("That was all, folks\n");
	exit(0);
}

int
main(void)
{
//	long a;
	int i;

//	a = sysconf(_SC_ATEXIT_MAX);
//	printf("ATEXIT_MAX = %ld\n", a);

	i = atexit(bye);
	if (i != 0) {
		fprintf(stderr, "cannot set exit function\n");
		return 1;
	}
	return 2;
}
