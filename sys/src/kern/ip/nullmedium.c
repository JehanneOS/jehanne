#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

static void
nullbind(Ipifc* _1, int _2, char** _3)
{
	error("cannot bind null device");
}

static void
nullunbind(Ipifc* _1)
{
}

static void
nullbwrite(Ipifc* _1, Block* _2, int _3, uint8_t* _4)
{
	error("nullbwrite");
}

Medium nullmedium =
{
.name=		"null",
.bind=		nullbind,
.unbind=	nullunbind,
.bwrite=	nullbwrite,
};

void
nullmediumlink(void)
{
	addipmedium(&nullmedium);
}
