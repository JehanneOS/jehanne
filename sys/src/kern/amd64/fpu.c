/*
 * Copyright (C) 2016-2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * SIMD Floating Point.
 * Assembler support to get at the individual instructions
 * is in l64fpu.S.
 * There are opportunities to be lazier about saving and
 * restoring the state and allocating the storage needed.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "amd64.h"
#include "ureg.h"

enum {						/* FCW, FSW and MXCSR */
	I		= 0x00000001,		/* Invalid-Operation */
	D		= 0x00000002,		/* Denormalized-Operand */
	Z		= 0x00000004,		/* Zero-Divide */
	O		= 0x00000008,		/* Overflow */
	U		= 0x00000010,		/* Underflow */
	P		= 0x00000020,		/* Precision */
};

enum {						/* FCW */
	PCs		= 0x00000000,		/* Precision Control -Single */
	PCd		= 0x00000200,		/* -Double */
	PCde		= 0x00000300,		/* -Double Extended */
	RCn		= 0x00000000,		/* Rounding Control -Nearest */
	RCd		= 0x00000400,		/* -Down */
	RCu		= 0x00000800,		/* -Up */
	RCz		= 0x00000C00,		/* -Toward Zero */
};

enum {						/* FSW */
	Sff		= 0x00000040,		/* Stack Fault Flag */
	Es		= 0x00000080,		/* Error Summary Status */
	C0		= 0x00000100,		/* ZF - Condition Code Bits */
	C1		= 0x00000200,		/* O/U# */
	C2		= 0x00000400,		/* PF */
	C3		= 0x00004000,		/* ZF */
	B		= 0x00008000,		/* Busy */
};

enum {						/* MXCSR */
	Daz		= 0x00000040,		/* Denormals are Zeros */
	Im		= 0x00000080,		/* I Mask */
	Dm		= 0x00000100,		/* D Mask */
	Zm		= 0x00000200,		/* Z Mask */
	Om		= 0x00000400,		/* O Mask */
	Um		= 0x00000800,		/* U Mask */
	Pm		= 0x00001000,		/* P Mask */
	Rn		= 0x00000000,		/* Round to Nearest */
	Rd		= 0x00002000,		/* Round Down */
	Ru		= 0x00004000,		/* Round Up */
	Rz		= 0x00006000,		/* Round toward Zero */
	Fz		= 0x00008000,		/* Flush to Zero for Um */
};

enum {						/* FPU.state */
	Init		= 0,			/* The FPU has not been used */
	Busy		= 1,			/* The FPU is being used */
	Idle		= 2,			/* The FPU has been used */

	Hold		= 4,			/* Handling an FPU note */
};

extern void _clts(void);
extern void _fldcw(uint16_t*);
extern void _fnclex(void);
extern void _fninit(void);
extern void _fxrstor(Fxsave*);
extern void _fxsave(Fxsave*);
extern void _fwait(void);
extern void _ldmxcsr(uint32_t*);
extern void _stts(void);

void
fpclear(void)
{
	_clts();
	_fnclex();
	_stts();
}

void
fpssesave(FPsave *fps)
{
	Fxsave *fx = (Fxsave*)ROUND(((uintptr_t)fps), FPalign);

	_fxsave(fx);
	_stts();
	if(fx != (Fxsave*)fps)
		memmove((Fxsave*)fps, fx, sizeof(Fxsave));
}
void
fpsserestore(FPsave *fps)
{
	Fxsave *fx = (Fxsave*)ROUND(((uintptr_t)fps), FPalign);

	if(fx != (Fxsave*)fps)
		memmove(fx, (Fxsave*)fps, sizeof(Fxsave));
	_clts();
	_fxrstor(fx);
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(unsigned int status, uintptr_t pc)
{
	char *msg, note[ERRMAX];
	int i;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=0x%lux",
		msg, pc, status);
	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg* _, void* __)
{
	/*
	 * Save FPU state to check out the error.
	 */
	fpsave(&up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave.fsw, up->fpsave.rip);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void* _)
{
	fpsave(&up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave.mxcsr & 0x3f, ureg->ip);
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void* _)
{
	unsigned int status, control;

	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		/*
		 * A process tries to use the FPU for the
		 * first time and generates a 'device not available'
		 * exception.
		 * Turn the FPU on and initialise it for use.
		 * Set the precision and mask the exceptions
		 * we don't care about from the generic Mach value.
		 */
		_clts();
		_fninit();
		_fwait();
		up->fpsave.fcw = 0x0232;
		_fldcw(&up->fpsave.fcw);
		up->fpsave.mxcsr = 0x1900;
		_ldmxcsr(&up->fpsave.mxcsr);
		up->fpstate = FPactive;
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions, there's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		status = up->fpsave.fsw;
		control = up->fpsave.fcw;
		if((status & ~control) & 0x07F){
			mathnote(status, up->fpsave.rip);
			break;
		}
		fprestore(&up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		panic("math emu pid %ld %s pc %#p",
			up->pid, up->text, ureg->ip);
		break;
	}
}

void
fpprocsetup(Proc* p)
{
	p->fpstate = FPinit;
	_stts();
}

void
fpprocfork(Proc *p)
{
	int s;

	/* save floating point state */
	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
		/* fallthrough */
	case FPinactive:
		p->fpsave = up->fpsave;
		p->fpstate = FPinactive;
	}
	splx(s);

}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg* _, void* __)
{
	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(X86FAMILY(m->cpuidax) == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
	trapenable(VectorSIMD, simderror, 0, "simderror");
}
