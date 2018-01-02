/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

/*
 *  8253 timer
 */
enum
{
	T0cntr=	0x40,		/* counter ports */
	T1cntr=	0x41,		/* ... */
	T2cntr=	0x42,		/* ... */
	Tmode=	0x43,		/* mode port (control word register) */
	T2ctl=	0x61,		/* counter 2 control port */

	/* commands */
	Latch0=	0x00,		/* latch counter 0's value */
	Load0l=	0x10,		/* load counter 0's lsb */
	Load0m=	0x20,		/* load counter 0's msb */
	Load0=	0x30,		/* load counter 0 with 2 bytes */

	Latch1=	0x40,		/* latch counter 1's value */
	Load1l=	0x50,		/* load counter 1's lsb */
	Load1m=	0x60,		/* load counter 1's msb */
	Load1=	0x70,		/* load counter 1 with 2 bytes */

	Latch2=	0x80,		/* latch counter 2's value */
	Load2l=	0x90,		/* load counter 2's lsb */
	Load2m=	0xa0,		/* load counter 2's msb */
	Load2=	0xb0,		/* load counter 2 with 2 bytes */

	/* 8254 read-back command: everything > pc-at has an 8254 */
	Rdback=	0xc0,		/* readback counters & status */
	Rdnstat=0x10,		/* don't read status */
	Rdncnt=	0x20,		/* don't read counter value */
	Rd0cntr=0x02,		/* read back for which counter */
	Rd1cntr=0x04,
	Rd2cntr=0x08,

	/* modes */
	ModeMsk=0xe,
	Square=	0x6,		/* periodic square wave */
	Trigger=0x0,		/* interrupt on terminal count */
	Sstrobe=0x8,		/* software triggered strobe */

	/* T2ctl bits */
	T2gate=	(1<<0),		/* enable T2 counting */
	T2spkr=	(1<<1),		/* connect T2 out to speaker */
	T2out=	(1<<5),		/* output of T2 */

	Freq=	1193182,	/* Real clock frequency */
	Tickshift=8,		/* extra accuracy */
	MaxPeriod=Freq/HZ,
	MinPeriod=Freq/(100*HZ),
};

typedef struct I8253 I8253;
struct I8253
{
	Lock;
	uint32_t	period;		/* current clock period */

	uint16_t	last;		/* last value of clock 1 */
	uint64_t	ticks;		/* cumulative ticks of counter 1 */

	uint32_t	periodset;
};
static I8253 i8253;

void
i8253reset(void)
{
	int loops, x;

	ilock(&i8253);

	i8253.last = 0;
	i8253.period = Freq/HZ;

	/*
	 *  enable a 1/HZ interrupt for providing scheduling interrupts
	 */
	outb(Tmode, Load0|Square);
	outb(T0cntr, (Freq/HZ));	/* low byte */
	outb(T0cntr, (Freq/HZ)>>8);	/* high byte */

	/*
	 *  enable a longer period counter to use as a clock
	 */
	outb(Tmode, Load2|Square);
	outb(T2cntr, 0);		/* low byte */
	outb(T2cntr, 0);		/* high byte */
	x = inb(T2ctl);
	x |= T2gate;
	outb(T2ctl, x);

	/*
	 * Introduce a little delay to make sure the count is
	 * latched and the timer is counting down; with a fast
	 * enough processor this may not be the case.
	 * The i8254 (which this probably is) has a read-back
	 * command which can be used to make sure the counting
	 * register has been written into the counting element.
	 */
	x = (Freq/HZ);
	for(loops = 0; loops < 100000 && x >= (Freq/HZ); loops++){
		outb(Tmode, Latch0);
		x = inb(T0cntr);
		x |= inb(T0cntr)<<8;
	}

	iunlock(&i8253);
}

void
i8253init(void)
{
	ioalloc(T0cntr, 4, 0, "i8253");
	ioalloc(T2ctl, 1, 0, "i8253.cntr2ctl");

	i8253reset();
}

void
guesscpuhz(int aalcycles)
{
	int loops, x, y;
	uint64_t a, b, cpufreq;

	if(m->machno != 0){
		m->cpuhz = MACHP(0)->cpuhz;
		m->cpumhz = MACHP(0)->cpumhz;
		m->loopconst = MACHP(0)->loopconst;
		return;
	}

	ilock(&i8253);
	for(loops = 1000;;loops += 1000) {
		/*
		 *  measure time for the loop
		 *
		 *			MOVL	loops,CX
		 *	aaml1:	 	AAM
		 *			LOOP	aaml1
		 *
		 *  the time for the loop should be independent of external
		 *  cache and memory system since it fits in the execution
		 *  prefetch buffer.
		 *
		 */
		outb(Tmode, Latch2);
		cycles(&a);
		x = inb(T2cntr);
		x |= inb(T2cntr)<<8;
		aamloop(loops);
		outb(Tmode, Latch2);
		cycles(&b);
		y = inb(T2cntr);
		y |= inb(T2cntr)<<8;

		x -= y;
		if(x < 0)
			x += 0x10000;

		if(x >= MaxPeriod || loops >= 1000000)
			break;
	}
	iunlock(&i8253);

	/* avoid division by zero on vmware 7 */
	if(x == 0)
		x = 1;

	/*
	 *  figure out clock frequency and a loop multiplier for delay().
	 *  n.b. counter goes up by 2*Freq
	 */
	cpufreq = (long)loops*((aalcycles*2*Freq)/x);
	m->loopconst = (cpufreq/1000)/aalcycles;	/* AAM+LOOP's for 1 ms */

	/* a == b means virtualbox has confused us */
	if(m->havetsc && b > a){
		b -= a;
		b *= 2*Freq;
		b /= x;
		m->cyclefreq = b;
		cpufreq = b;
	}
	m->cpuhz = cpufreq;

	/*
	 *  round to the nearest megahz
	 */
	m->cpumhz = (cpufreq+500000)/1000000L;
	if(m->cpumhz == 0)
		m->cpumhz = 1;
}

void
i8253timerset(uint64_t next)
{
	int period;
	uint32_t want;
	uint32_t now;

	want = next>>Tickshift;
	now = i8253.ticks;	/* assuming whomever called us just did fastticks() */

	period = want - now;
	if(period < MinPeriod)
		period = MinPeriod;
	else if(period > MaxPeriod)
		period = MaxPeriod;

	/* hysteresis */
	if(i8253.period != period){
		ilock(&i8253);
		/* load new value */
		outb(Tmode, Load0|Square);
		outb(T0cntr, period);		/* low byte */
		outb(T0cntr, period >> 8);	/* high byte */

		/* remember period */
		i8253.period = period;
		i8253.periodset++;
		iunlock(&i8253);
	}
}

static void
i8253clock(Ureg* ureg, void* _)
{
	timerintr(ureg, 0);
}

void
i8253enable(void)
{
	intrenable(IrqCLOCK, i8253clock, 0, BUSUNKNOWN, "clock");
}

/*
 *  return the total ticks of counter 2.  We shift by
 *  8 to give timesync more wriggle room for interpretation
 *  of the frequency
 */
uint64_t
i8253read(uint64_t *hz)
{
	uint16_t y, x;
	uint64_t ticks;

	if(hz)
		*hz = Freq<<Tickshift;

	ilock(&i8253);
	outb(Tmode, Latch2);
	y = inb(T2cntr);
	y |= inb(T2cntr)<<8;

	if(y < i8253.last)
		x = i8253.last - y;
	else {
		x = i8253.last + (0x10000 - y);
		if (x > 3*MaxPeriod) {
			outb(Tmode, Load2|Square);
			outb(T2cntr, 0);		/* low byte */
			outb(T2cntr, 0);		/* high byte */
			y = 0xFFFF;
			x = i8253.period;
		}
	}
	i8253.last = y;
	i8253.ticks += x>>1;
	ticks = i8253.ticks;
	iunlock(&i8253);

	return ticks<<Tickshift;
}

void
delay(int millisecs)
{
	millisecs *= m->loopconst;
	if(millisecs <= 0)
		millisecs = 1;
	aamloop(millisecs);
}

void
microdelay(int microsecs)
{
	microsecs *= m->loopconst;
	microsecs /= 1000;
	if(microsecs <= 0)
		microsecs = 1;
	aamloop(microsecs);
}

/*
 *  performance measurement ticks.  must be low overhead.
 *  doesn't have to count over a second.
 */
uint64_t
perfticks(void)
{
	uint64_t x;

	if(m->havetsc)
		cycles(&x);
	else
		x = 0;
	return x;
}
