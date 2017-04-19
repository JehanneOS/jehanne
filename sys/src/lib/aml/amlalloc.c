#include <u.h>
#include <lib9.h>
#include <aml.h>

void*
amlalloc(int n)
{
	return mallocz(n, 1);
}

void
amlfree(void *p)
{
	free(p);
}
