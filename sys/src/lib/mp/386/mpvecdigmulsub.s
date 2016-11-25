/*
 *	mpvecdigmulsub(mpdigit *b, int n, mpdigit m, mpdigit *p)
 *
 *	p -= b*m
 *
 *	each step look like:
 *		hi,lo = m*b[i]
 *		lo += oldhi + carry
 *		hi += carry
 *		p[i] += lo
 *		oldhi = hi
 *
 *	the registers are:
 *		hi = DX		- constrained by hardware
 *		lo = AX		- constrained by hardware
 *		b = SI		- can't be BP
 *		p = DI		- can't be BP
 *		i = BP
 *		n = CX		- constrained by LOOP instr
 *		m = BX
 *		oldhi = EX
 *		
 */
TEXT	mpvecdigmulsub(SB),$4

	MOVL	b+0(FP),SI
	MOVL	n+4(FP),CX
	MOVL	m+8(FP),BX
	MOVL	p+12(FP),DI
	XORL	BP,BP
	MOVL	BP,0(SP)
_mulsubloop:
	MOVL	(SI)(BP*4),AX		/* lo = b[i] */
	MULL	BX			/* hi, lo = b[i] * m */
	ADDL	0(SP),AX		/* lo += oldhi */
	ADCL	$0, DX			/* hi += carry */
	SUBL	AX,(DI)(BP*4)
	ADCL	$0, DX			/* hi += carry */
	MOVL	DX,0(SP)
	INCL	BP
	LOOP	_mulsubloop
	MOVL	CX, AX
	MOVL	0(SP),BX
	SUBL	BX,(DI)(BP*4)
	SBBL	CX, AX
	ORL	$1, AX
	RET
