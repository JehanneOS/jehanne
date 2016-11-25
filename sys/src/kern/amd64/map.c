#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * Before mmuinit is done, we can't rely on sys->vmunmapped
 * being set, so we use the static limit TMFM
 */

void*
KADDR(uintmem pa)
{
	uint8_t* va;

	va = UINT2PTR(pa);
	if(sys->vmunmapped != 0){
		if(pa < sys->vmunmapped-KSEG0)
			return KSEG0+va;
	}else	if(pa < TMFM)
		return KSEG0+va;
	return KSEG2+va;
}

uintmem
PADDR(void* va)
{
	uintmem pa;

	pa = PTR2UINT(va);
	if(pa >= KSEG2 && pa < KSEG1)
		return pa-KSEG2;
	if(pa >= KSEG0 && pa < KSEG0+TMFM)
		return pa-KSEG0;
	if(pa > KSEG2)
		return pa-KSEG2;

	panic("PADDR: va %#p pa #%p @ %#p\n", va, mmuphysaddr(PTR2UINT(va)), getcallerpc());
	return 0;
}

