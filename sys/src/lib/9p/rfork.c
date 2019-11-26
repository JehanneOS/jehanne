#include <u.h>
#include <lib9.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>

static void
rforker(void (*fn)(void*), void *arg, int flag)
{
	switch(sys_rfork(RFPROC|RFMEM|RFNOWAIT|flag)){
	case -1:
		sysfatal("rfork: %r");
	default:
		return;
	case 0:
		fn(arg);
		sys__exits(0);
	}
}

void
listensrv(Srv *s, char *addr)
{
	_forker = rforker;
	_listensrv(s, addr);
}

void
postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	_forker = rforker;
	_postmountsrv(s, name, mtpt, flag);
}

void
postsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	_forker = rforker;
	_postsharesrv(s, name, mtpt, desc);
}
