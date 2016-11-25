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
 *		oldhi = R8
 *		
 */
TEXT	mpvecdigmulsub(SB),$0
	MOVQ	RARG,SI
	MOVL	n+8(FP),CX
	MOVL	m+16(FP),BX
	MOVQ	p+24(FP),DI
	XORL	BP,BP
	MOVL	BP,R8
_mulsubloop:
	MOVL	(SI)(BP*4),AX		/* lo = b[i] */
	MULL	BX			/* hi, lo = b[i] * m */
	ADDL	R8,AX		/* lo += oldhi */
	ADCL	$0, DX		/* hi += carry */
	SUBL	AX,(DI)(BP*4)
	ADCL	$0, DX		/* hi += carry */
	MOVL	DX,R8
	INCL	BP
	LOOP	_mulsubloop
	MOVL	CX, AX
	SUBL	R8,(DI)(BP*4)
	SBBQ	CX, AX
	ORQ	$1, AX
	RET
