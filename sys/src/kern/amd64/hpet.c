#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

typedef struct Hpet Hpet;
typedef struct Tn Tn;

struct Tn {					/* Timer */
	uint32_t	cnf;				/* Configuration */
	uint32_t	cap;				/* Capabilities */
	uint64_t	comparator;			/* Comparator */
	uint32_t	val;				/* FSB Interrupt Value */
	uint32_t	addr;				/* FSB Interrupt Address */
	uint32_t	_24_[2];
};

struct Hpet {					/* Event Timer Block */
	uint32_t	cap;				/* General Capabilities */
	uint32_t	period;				/* Main Counter Tick Period */
	uint32_t	_8_[2];
	uint32_t	cnf;				/* General Configuration */
	uint32_t	_20_[3];
	uint32_t	sts;				/* General Interrupt Status */
	uint32_t	_36_[51];
	uint64_t	counter;			/* Main Counter Value */
	uint32_t	_248[2];
	Tn		tn[24];				/* Timers */
};

static Hpet* etb[8];				/* Event Timer Blocks */
static uint64_t zerostamp;
static uint64_t *stamper = &zerostamp;		/* hpet counter used for time stamps, or 0 if no hpet */
static uint32_t period;		/* period of active hpet */

uint64_t
hpetticks(uint64_t* _1)
{
	return *stamper;
}

uint64_t
hpetticks2ns(uint64_t ticks)
{
	return ticks*period / 1000 / 1000;
}

uint64_t
hpetticks2us(uint64_t ticks)
{
	return hpetticks2ns(ticks) / 1000;
}

/*
 * called from acpi
 */
void
hpetinit(uint32_t id, uint32_t seqno, uintmem pa, int minticks)
{
	Tn *tn;
	int i, n;
	Hpet *hpet;

	jehanne_print("hpet: id %#ux seqno %d pa %#P minticks %d\n", id, seqno, pa, minticks);
	if(seqno >= nelem(etb))
		return;
	if((hpet = vmap(pa, 1024)) == nil)		/* HPET §3.2.4 */
		return;
	memreserve(pa, 1024);
	etb[seqno] = hpet;

	jehanne_print("HPET: cap %#8.8ux period %#8.8ux\n", hpet->cap, hpet->period);
	jehanne_print("HPET: cnf %#8.8ux sts %#8.8ux\n",hpet->cnf, hpet->sts);
	jehanne_print("HPET: counter %#.16llux\n", hpet->counter);

	n = ((hpet->cap>>8) & 0x0F) + 1;
	for(i = 0; i < n; i++){
		tn = &hpet->tn[i];
		DBG("Tn%d: cnf %#8.8ux cap %#8.8ux\n", i, tn->cnf, tn->cap);
		DBG("Tn%d: comparator %#.16llux\n", i, tn->comparator);
		DBG("Tn%d: val %#8.8ux addr %#8.8ux\n", i, tn->val, tn->addr);
		USED(tn);
	}
	/*
	 * hpet->period is the number of femtoseconds per counter tick.
	 */

	/*
	 * activate the first hpet as the source of time stamps
	 */
	if(seqno == 0){
		period = hpet->period;
		stamper = &hpet->counter;
		/* the timer block must be enabled to start the main counter for timestamping */
		hpet->cap |= 1<<0;	/* ENABLE_CNF */
	}
}
