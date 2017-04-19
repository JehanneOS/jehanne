#include "os.h"
#include <mp.h>
#include "dat.h"

// convert an mpint into a big endian byte array (most significant byte first; left adjusted)
//   return number of bytes converted
//   if p == nil, allocate and result array
int
mptobe(mpint *b, uint8_t *p, uint32_t n, uint8_t **pp)
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
			jehanne_sysfatal("mptobe: %r");
		jehanne_setmalloctag(p, jehanne_getcallerpc());
	} else {
		if(n < m)
			return -1;
		if(n > m)
			jehanne_memset(p+m, 0, n-m);
	}
	if(pp != nil)
		*pp = p;
	mptober(b, p, m);
	return m;
}
