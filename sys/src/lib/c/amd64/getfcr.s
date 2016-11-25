/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
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

.globl setfcr
setfcr:
	xor	$(0x3F<<7), %edi
	and	$0xFFC0, %edi
	wait
	stmxcsr	-8(%rsp)
	mov	-8(%rsp), %eax
	and	$~0x3F,  %eax
	or	%edi,  %eax
	mov	%eax, -8(%rsp)
	ldmxcsr	-8(%rsp)
	ret

.globl getfcr
getfcr:
	wait
	stmxcsr	-8(%rsp)
	movzx	-8(%rsp), %ax
	andl	$0xFFC0, %eax
	xorl	$(0x3F<<7), %eax
	ret

.globl getfsr
	wait
	stmxcsr	-8(%rsp)
	mov	-8(%rsp), %eax
	and	$0x3F, %eax
	RET

.globl setfsr
setfsr:
	and	$0x3F, %edi
	wait
	stmxcsr	-8(%rsp)
	mov	-8(%rsp), %eax
	and	$~0x3F, %eax
	or	%edi, %eax
	mov	%eax, -8(%rsp)
	ldmxcsr	-8(%rsp)
	ret
