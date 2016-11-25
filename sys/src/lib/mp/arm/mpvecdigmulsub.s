TEXT mpvecdigmulsub(SB),$0
	MOVW	n+4(FP),R4
	MOVW	m+8(FP),R5
	MOVW	p+12(FP),R6
	MOVW	$0, R2
_mulsubloop:
	MOVW	$0, R1
	MOVW.P	4(R0), R3
	MULALU	R3, R5, (R1, R2)
 	MOVW	(R6), R7
	SUB.S	R2, R7
	ADD.CC	$1, R1
	MOVW	R1, R2
	MOVW.P	R7, 4(R6)
	SUB.S	$1, R4
	B.NE	_mulsubloop
	MOVW	(R6), R7
	SUB.S	R2, R7
	MOVW.CS	$1, R0
	MOVW.CC	$-1, R0
	MOVW	R7, (R6)
	RET
