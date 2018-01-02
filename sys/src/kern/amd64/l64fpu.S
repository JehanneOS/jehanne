/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

/*
 * SIMD Floating Point.
 */
.globl _clts
_clts:
	clts
	ret

.globl _fldcw
_fldcw:
	fldcw	(%rdi)
	ret

.globl _fnclex
_fnclex:
	fclex
	ret

.globl _fninit
_fninit:
	finit					/* no WAIT */
	ret

.globl _fxrstor
_fxrstor:
	fxrstor64 0(%rdi)
	ret

.globl _fxsave
_fxsave:
	fxsave64 0(%rdi)
	ret

.globl _fwait
_fwait:
	wait
	ret

.globl _ldmxcsr
_ldmxcsr:
	ldmxcsr	(%rdi)
	ret

.globl _stts
_stts:
	movq	%cr0, %rax
	orq	$8, %rax			/* Ts */
	movq	%rax, %cr0
	ret
