#include "os.h"
#include <libsec.h>

/* 
 *  use the X917 random number generator to create random
 *  numbers (faster than jehanne_truerand() but not as random).
 */
uint32_t
fastrand(void)
{
	uint32_t x;
	
	genrandom((uint8_t*)&x, sizeof x);
	return x;
}
