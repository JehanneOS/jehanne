TEXT mpvecdigmuladd(SB),$0
	MOVW	n+4(FP),R4
	MOVW	m+8(FP),R5
	MOVW	p+12(FP),R6
	MOVW	$0, R2
_muladdloop:
	MOVW	$0, R1
	MOVW.P	4(R0), R3
	MULALU	R3, R5, (R1, R2)
 	MOVW	(R6), R7
	ADD.S	R2, R7
	ADC	$0, R1, R2
	MOVW.P	R7, 4(R6)
	SUB.S	$1, R4
	B.NE	_muladdloop
	MOVW	(R6), R7
	ADD	R2, R7
	MOVW	R7, (R6)
	RET
