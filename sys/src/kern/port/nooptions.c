#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * No options from configuration (add amd64/options.c if you need them)
 */

void
confoptions(void)
{
}

char*
getconf(char *name)
{
	return 0;
}

void
confsetenv(void)
{
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	return 0;
}
