TEXT mpvecsub(SB),$0
	MOVW	alen+4(FP), R4
	MOVW	b+8(FP), R5
	MOVW	blen+12(FP), R6
	MOVW	diff+16(FP), R7
	MOVW	$0, R8
	MOVW	R8, R3
	CMP	R8, R6
	B.EQ	_sub1
	SUB	R6, R4, R4
_subloop1:
	MOVW.P	4(R0), R1
	MOVW.P	4(R5), R2
	CMP	R3, R8
	SBC.S	R2, R1
	SBC	R8, R8, R3
	MOVW.P	R1, 4(R7)
	SUB.S	$1, R6
	B.NE	_subloop1
_sub1:
	CMP	R8, R4
	RET.EQ
_subloop2:
	MOVW.P	4(R0), R1
	CMP	R3, R8
	SBC.S	R8, R1
	SBC	R8, R8, R3
	MOVW.P	R1, 4(R7)
	SUB.S	$1, R4
	B.NE	_subloop2
	RET
