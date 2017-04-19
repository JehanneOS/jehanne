#include <u.h>
#include <libc.h>

static Lock	privlock;

extern void	**_privates;
extern int	_nprivates;

void **
jehanne_privalloc(void)
{
	void **p;

	jehanne_lock(&privlock);
	if(_nprivates > 0)
		p = &_privates[--_nprivates];
	else
		p = nil;
	jehanne_unlock(&privlock);

	return p;
}
