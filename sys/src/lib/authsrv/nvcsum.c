#include <u.h>
#include <lib9.h>
#include <auth.h>

uint8_t
nvcsum(void *vmem, int n)
{
	uint8_t *mem, sum;
	int i;

	sum = 9;
	mem = vmem;
	for(i = 0; i < n; i++)
		sum += mem[i];
	return sum;
}
