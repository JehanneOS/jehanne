#include <u.h>
#include <libc.h>

static Lock	privlock;

extern void	**_privates;
extern int	_nprivates;

void **
privalloc(void)
{
	void **p;

	lock(&privlock);
	if(_nprivates > 0)
		p = &_privates[--_nprivates];
	else
		p = nil;
	unlock(&privlock);

	return p;
}
