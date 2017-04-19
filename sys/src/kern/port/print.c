#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static Lock fmtl;

void
_fmtlock(void)
{
	lock(&fmtl);
}

void
_fmtunlock(void)
{
	unlock(&fmtl);
}

int
_efgfmt(Fmt* _1)
{
	return -1;
}

int
mregfmt(Fmt* f)
{
	Mreg mreg;

	mreg = va_arg(f->args, Mreg);
	if(sizeof(Mreg) == sizeof(uint64_t))
		return jehanne_fmtprint(f, "%#16.16llux", (uint64_t)mreg);
	return jehanne_fmtprint(f, "%#8.8ux", (uint32_t)mreg);
}

void
fmtinit(void)
{
	jehanne_quotefmtinstall();
	archfmtinstall();
}
