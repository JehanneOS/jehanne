#include "os.h"
#include <mp.h>
#include "dat.h"

// convert an mpint into a little endian byte array (least significant byte first)
//   return number of bytes converted
//   if p == nil, allocate and result array
int
mptole(mpint *b, uint8_t *p, uint32_t n, uint8_t **pp)
{
	int m;

	assert((p == nil) != (pp == nil));
	m = (mpsignif(b)+7)/8;
	if(m == 0)
		m++;
	if(p == nil){
		n = m;
		p = jehanne_malloc(n);
		if(p == nil)
			jehanne_sysfatal("mptole: %r");
		jehanne_setmalloctag(p, jehanne_getcallerpc());
	} else if(n < m)
		return -1;
	if(pp != nil)
		*pp = p;
	mptolel(b, p, n);
	return m;
}
