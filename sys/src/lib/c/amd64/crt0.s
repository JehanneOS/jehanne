/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
.text

.globl	_main
_main:
	/* clear the frame pointer */
	xorq %rbp, %rbp

	/* set argc (%rdi) and argv (%rsi) */
	movq	0(%rsp), %rdi
	leaq	8(%rsp), %rsi

	/* set _mainpid */
	movl	%r12d, _mainpid

	movq	%rsp, %rbp
	pushq	%rsi
	pushq	%rdi
	call	_init
	popq	%rdi
	popq	%rsi

	call	__jehanne_libc_init

.size _main,.-_main
