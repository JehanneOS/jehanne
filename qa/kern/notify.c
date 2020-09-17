#include <u.h>
#include <lib9.h>
#define RET 0xc3

void
handler(void *v, char *s)
{
	print("PASS\n");
	exits("PASS");
}

void
main(void)
{
	void (*f)(void) = nil;
	if (sys_notify(handler)){
		fprint(2, "%r\n");
		exits("sys_notify fails");
	}

	f();
	print("FAIL");
	exits("FAIL");
}
