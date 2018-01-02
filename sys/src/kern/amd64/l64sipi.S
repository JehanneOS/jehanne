/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

/*
 * Start-up request IPI handler.
 *
 * This code is executed on an application processor in response to receiving
 * a Start-up IPI (SIPI) from another processor.
 * This must be placed on a 4KiB boundary
 * somewhere in the 1st MiB of conventional memory. However,
 * due to some shortcuts below it's restricted further to within the 1st 64KiB.
 * The AP starts in real-mode, with
 *   CS selector set to the startup memory address/16;
 *   CS base set to startup memory address;
 *   CS limit set to 64KiB;
 *   CPL and IP set to 0.
 */
#include "mem.h"
#include "amd64.h"
#ifndef __ASSEMBLER__
#define __ASSEMBLER__
#endif

.section .text
.code16

.align 4
apbootstrap:
	ljmp	$0x0, $_apbootstrap
	nop
	nop
	nop
_apvector:			/* address APBOOTSTRAP+0x08 */
	.quad	0
_appml4:			/* address APBOOTSTRAP+0x10 */
	.quad	0
_apapic:			/* address APBOOTSTRAP+0x18 */
	.quad	0
_apmach:			/* address APBOOTSTRAP+0x20 */
	.quad	0
_apbootstrap:
	mov	%cs, %ax
	mov	%ax, %ds
	lgdt	_gdtptr32p
	movl	%cr0, %eax
	or	$0x1, %ax
	mov	%eax, %cr0
	jmp	1f
1:
	ljmp	$0x18, $_ap32

.code32
_ap32:
	movl	$SELECTOR(2, SELGDT, 0), %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	movl	_appml4, %eax		/* physical address of PML4 */
	movl	%eax, %cr3		/* load the mmu */
	jmp	2f
2:
	movl	%cr4, %eax
	andl	$0xffffffef, %eax	/* Page Size (~0x00000010 = 0xffffffef) */
	orl	$0xa0, %eax		/* Page Global, Phys. Address */
	movl	%eax, %cr4

	movl	$0xc0000080, %ecx	/* Extended Feature Enable */
	rdmsr
	orl	$0x00000100, %eax	/* Long Mode Enable */
	wrmsr

	movl	%cr0, %edx
	andl	$0x9ffffff5, %edx	/* (~0x6000000a = 0x9ffffff5) */
	orl	$0x80010000, %edx	/* Paging Enable, Write Protect */
	movl	%edx, %cr0

	ljmp	$0x8, $(_ap64)

.code64
_ap64:
	movq	$_gdtptr64v, %rax
	lgdt	(%rax)

	xorq	%rax, %rax
	movw	%ax, %ds		/* not used in long mode */
	movw	%ax, %es		/* not used in long mode */
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss		/* not used in long mode */

	lldt	%ax

	movq	_apmach, %rsp

	movq	%rax, %r14		/* up = nil; */
	movq	%rsp, %r15		/* m = apmach */

	addq	$MACHSIZE, %rsp

	pushq	%rax			/* clear flags */
	popfq

	movq	_apvector, %rax
	movq	_apapic, %rdi
	pushq	%rbp

	call	*%rax

_halt:
	hlt
	jmp	_halt

.align 16
_gdt:
	/* null descriptor */
	.long	0
	.long	0

	/* (KESEG) 64 bit long mode exec segment */
	.long	0xFFFF
	.long	SEGL|SEGG|SEGP|(0xF<<16)|SEGPL(0)|SEGEXEC|SEGR

	/* 32 bit data segment descriptor for 4 gigabytes (PL 0) */
	.long	0xFFFF
	.long	SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(0)|SEGDATA|SEGW

	/* 32 bit exec segment descriptor for 4 gigabytes (PL 0) */
	.long	0xFFFF
	.long	SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(0)|SEGEXEC|SEGR

.align 4
_gdtptr32p:
	.word	4*8-1
	.long	_gdt-KZERO

.align 4
_gdtptr64p:
	.word	4*8-1
	.quad	_gdt-KZERO

.align 4
_gdtptr64v:
	.word	4*8-1
	.quad	_gdt
