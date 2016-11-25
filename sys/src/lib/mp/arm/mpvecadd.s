TEXT mpvecadd(SB),$0
	MOVW	alen+4(FP), R4
	MOVW	b+8(FP), R5
	MOVW	blen+12(FP), R6
	MOVW	sum+16(FP), R7
	MOVW	$0, R8
	MOVW	R8, R3
	CMP	R8, R6
	B.EQ	_add1
	SUB	R6, R4, R4
_addloop1:
	MOVW.P	4(R0), R1
	MOVW.P	4(R5), R2
	CMP	$1, R3
	ADC.S	R2, R1
	ADC	R8, R8, R3
	MOVW.P	R1, 4(R7)
	SUB.S	$1, R6
	B.NE	_addloop1
_add1:
	CMP	R8, R4
	B.EQ	_addend
_addloop2:
	MOVW.P	4(R0), R1
	ADD.S	R3, R1
	ADC	R8, R8, R3
	MOVW.P	R1, 4(R7)
	SUB.S	$1, R4
	B.NE	_addloop2
_addend:
	MOVW	R3, (R7)
	RET
