#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <bio.h>
#include <ndb.h>

int
authdial(char *netroot, char *dom)
{
	Ndbtuple *t, *nt;
	char *p;
	int rv;

	if(dom == nil)
		/* look for one relative to my machine */
		return dial(netmkaddr("$auth", nil, "ticket"), nil, nil, nil);

	/* look up an auth server in an authentication domain */
	p = csgetvalue(netroot, "authdom", dom, "auth", &t);

	/* if that didn't work, just try the IP domain */
	if(p == nil)
		p = csgetvalue(netroot, "dom", dom, "auth", &t);

	/*
	 * if that didn't work, try p9auth.$dom.  this is very helpful if
	 * you can't edit /lib/ndb.
	 */
	if(p == nil) {
		p = smprint("p9auth.%s", dom);
		t = ndbnew("auth", p);
	}
	free(p);

	/*
	 * allow multiple auth= attributes for backup auth servers,
	 * try each one in order.
	 */
	rv = -1;
	for(nt = t; nt != nil; nt = nt->entry) {
		if(strcmp(nt->attr, "auth") == 0) {
			rv = dial(netmkaddr(nt->val, nil, "ticket"), nil, nil, nil);
			if(rv >= 0)
				break;
		}
	}
	ndbfree(t);

	return rv;
}
