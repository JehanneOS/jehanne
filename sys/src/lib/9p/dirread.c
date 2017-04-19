#include <u.h>
#include <lib9.h>
#include <auth.h>
#include <9P2000.h>
#include <thread.h>
#include <9p.h>

void
dirread9p(Req *r, Dirgen *gen, void *aux)
{
	int start;
	uint8_t *p, *ep;
	uint32_t rv;
	Dir d;

	if(r->ifcall.offset == 0)
		start = 0;
	else
		start = r->fid->dirindex;

	p = (uint8_t*)r->ofcall.data;
	ep = p+r->ifcall.count;

	while(p < ep){
		memset(&d, 0, sizeof d);
		if((*gen)(start, &d, aux) < 0)
			break;
		rv = convD2M(&d, p, ep-p);
		free(d.name);
		free(d.muid);
		free(d.uid);
		free(d.gid);
		if(rv <= BIT16SZ)
			break;
		p += rv;
		start++;
	}
	r->fid->dirindex = start;
	r->ofcall.count = p - (uint8_t*)r->ofcall.data;
}
