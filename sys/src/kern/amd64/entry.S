/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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
#include "mem.h"
#include "amd64.h"
#ifndef __ASSEMBLER__
#define __ASSEMBLER__
#endif
#include "multiboot.h"

.code32

/* This code (up to _start64v) is linked and loaded at physical address
 * 0x00100000 (1MB), which is the start of extended memory. (See kernel.ld)
 */

/* boottext must be text: http://sourceware.org/binutils/docs/as/Section.html
 */
.section .boottext, "awx"

.align 4
#define MBFLAGS (MULTIBOOT_MEMORY_INFO | MULTIBOOT_PAGE_ALIGN)
_multibootheader:
	.long	MULTIBOOT_HEADER_MAGIC
	.long	MBFLAGS
	.long	(-(MULTIBOOT_HEADER_MAGIC + MBFLAGS))	/* checksum */

.align 16
.globl _boot_registers
_boot_registers:
_ax:
	.quad 0
_bx:
	.quad 0
_cx:
	.quad 0
_dx:
	.quad 0
_si:
	.quad 0
_di:
	.quad 0
_bp:
	.quad 0
_r8:
	.quad 0
_r9:
	.quad 0
_r10:
	.quad 0
_r11:
	.quad 0
_r12:
	.quad 0
_r13:
	.quad 0
_r14:
	.quad 0
_r15:
	.quad 0
_type:
	.quad 0
_error:
	.quad 0
_ip:
	.quad 0
_cs:
	.quad 0
_flags:
	.quad 0
_sp:
	.quad 0
_ss:
	.quad 0

.globl _start
_start:
	cli
	lgdt	%cs:_gdtptr32p
	ljmp	$0x18, $_protected

_protected:
	/* save initial registers (at least those available so far) */
	movl	%eax, _ax
	movl	%ebx, _bx
	movl	%ecx, _cx
	movl	%edx, _dx
	movl	%ebp, _bp
	movl	%esp, _sp
	movl	%esi, _si
	movl	%edi, _di

	movl	$SELECTOR(2, SELGDT, 0), %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	jmp	_warp64

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
	.long	_gdt

.align 4
_gdtptr64p:
	.word	4*8-1
	.quad	_gdt

/*
 * Macros for accessing page table entries; change the
 * C-style array-index macros into a page table byte offset
 */
#define PML4O(v)	((PTLX((v), 3))<<3)
#define PDPO(v)		((PTLX((v), 2))<<3)
#define PDO(v)		((PTLX((v), 1))<<3)
#define PTO(v)		((PTLX((v), 0))<<3)

_warp64:
	movl	$((CPU0END-CPU0PML4)>>2), %ecx
	movl	$(CPU0PML4-KZERO), %esi

	movl	%esi, %edi
	xorl	%eax, %eax

	cld
	rep;	stosl

	movl	%esi, %eax				/* PML4 */
	movl	%eax, %edx
	addl	$(PTSZ|PTEWRITE|PTEVALID), %edx		/* PDP at PML4 + PTSZ */
	movl	%edx, PML4O(0)(%eax)			/* PML4E for double-map */
	movl	%edx, PML4O(KZERO)(%eax)		/* PML4E for KZERO */

	addl	$PTSZ, %eax				/* PDP at PML4 + PTSZ */
	addl	$PTSZ, %edx				/* PD0 at PML4 + 2*PTSZ */
	movl	%edx, PDPO(0)(%eax)			/* PDPE for double-map */
	movl	%edx, PDPO(KZERO)(%eax)			/* PDPE for KZERO */

	/*
	 * add PDPE for KZERO+1GB early as Vmware
	 * hangs when modifying kernel PDP
	 */
	addl	$PTSZ, %edx				/* PD1 */
	movl	%edx, PDPO(KZERO+GiB)(%eax)

	addl	$PTSZ, %eax				/* PD0 at PML4 + 2*PTSZ */
	movl	$(PTESIZE|PTEGLOBAL|PTEWRITE|PTEVALID), %edx
	movl	%edx, PDO(0)(%eax)			/* PDE for double-map */

	/*
	 * map from KZERO to end using 2MB pages
	 */
	addl	$PDO(KZERO), %eax
	movl	$end-KZERO, %ecx

	addl    $(16*MiB), %ecx				/* qemu puts multiboot data after the kernel, including initrd */

	addl	$(PGLSZ(1)-1), %ecx
	andl	$(~(PGLSZ(1)-1)), %ecx
	movl	%ecx, MemMin-KZERO			/* see memory.c */
	shr	$(PTSHFT+PGSHIFT), %ecx

memloop:
	movl	%edx, (%eax)
	addl	$PGLSZ(1), %edx
	addl	$8, %eax
	loop	memloop

/*
 * Enable and activate Long Mode. From the manual:
 * 	make sure Page Size Extentions are off, and Page Global
 *	Extensions and Physical Address Extensions are on in CR4;
 *	set Long Mode Enable in the Extended Feature Enable MSR;
 *	set Paging Enable in CR0;
 *	make an inter-segment jump to the Long Mode code.
 * It`s all in 32-bit mode until the jump is made.
 */
lme:
	movl	%esi, %cr3			/* load the mmu */
	jmp	1f
1:
	movl	%cr4, %eax
	andl	$~Pse, %eax			/* Page Size */
	orl	$(Pge|Pae), %eax		/* Page Global, Phys. Address */
	movl	%eax, %cr4

	movl	$Efer, %ecx			/* Extended Feature Enable */
	rdmsr
	orl	$Lme, %eax			/* Long Mode Enable */
	wrmsr

	movl	%cr0, %edx
	andl	$~(Cd|Nw|Ts|Mp), %edx
	orl	$(Pg|Wp), %edx			/* Paging Enable */
	movl	%edx, %cr0

	ljmp 	$0x8, $_identity


/*
 * Long mode. Welcome to 2003.
 * Jump out of the identity map space;
 * load a proper long mode GDT.
 */
.code64

_identity:
	/* save initial registers */
	movq	%r8, _r8
	movq	%r9, _r9
	movq	%r10, _r10
	movq	%r11, _r11
	movq	%r12, _r12
	movq	%r13, _r13
	movq	%r14, _r14
	movq	%r15, _r15

	movq	$_start64v, %rax
	jmp	*%rax

.section .text

.align 4
.globl _gdtptr64v
_gdtptr64v:
	.word	4*8-1
	.quad	_gdt+KZERO
	.word	0

.align 4
_start64v:
	movq	$_gdtptr64v, %rax
	lgdt	(%rax)

	xorq	%rax, %rax
	movw	%ax, %ds			/* not used in long mode */
	movw	%ax, %es			/* not used in long mode */
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss			/* not used in long mode */

	lldt	%ax

	movq	$(CPU0MACH+MACHSIZE), %rsp
	movq	$CPU0MACH, %r15			/* m = CPU0MACH */
	movq	%rax, %r14			/* up = 0; */

_clearbss:
	movq	$edata, %rdi
	movq	$end, %rcx
	addq	$(PGSZ-1), %rdi
	andq	$(~(PGSZ-1)), %rdi
	subq	%rdi, %rcx			/* end-edata bytes */
	shrq	$2, %rcx			/* end-edata doublewords */

	cld
	rep;	stosl

	pushq	%rax
	popfq

	call	main

.globl ndnr
ndnr:	/* no deposit, no return */
	/* do not resuscitate */
_dnr:
	sti
	hlt
	jmp	_dnr				/* do not resuscitate */
