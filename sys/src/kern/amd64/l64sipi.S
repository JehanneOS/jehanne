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
 * Parameters are passed to this code via a vector in low memory
 * indexed by the APIC number of the processor. The layout, size,
 * and location have to be kept in sync with the setup in sipi.s.
 */
#include "mem.h"
#include "amd64.h"
#ifndef __ASSEMBLER__
#define __ASSEMBLER__
#endif

.section .text, "awx"

/*
 * Real mode. Welcome to 1978.
 * Load a basic GDT, turn on protected mode and make
 * inter-segment jump to the protected mode code.
 */
.code16
.align 4096
.globl sipihandler
sipihandler:
_real:
	ljmp	$0x0, $_endofheader

_startofheader:
	NOP; NOP; NOP
	.quad	0xa5a5a5a5a5a5a5a5

_gdt32p:
	.quad	0x0000000000000000		/* NULL descriptor */
	.quad	0x00cf9a000000ffff		/* CS */
	.quad	0x00cf92000000ffff		/* DS */
	.quad	0x0020980000000000		/* .long mode CS */

_gdtptr32p:
	.word	4*8-1				/* includes .long mode */
	.long	_gdt32p

_gdt64:
	.quad	0x0000000000000000		/* NULL descriptor */
	.quad	0x0020980000000000		/* CS */
	.quad	0x0000800000000000		/* DS */

_gdtptr64v:
	.word	3*8-1
	.quad	_gdt64

_endofheader:
	mov	%cs, %ax
	mov	%ax, %ds

	lgdt	_gdtptr32p			/* load a basic gdt */

	mov	%cr0, %eax
	or	$Pe, %ax
	mov	%eax, %cr0			/* turn on protected mode */

	jmp	1f
1:
	mov	$(SSEL(SiDS, SsTIGDT|SsRPL0)), %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	mov	%ax, %ss

	ljmpl	$8, $_protected			/* 8 = SSEL(SiCS, SsTIGDT|SsRPL0) */


/*
 * Protected mode. Welcome to 1982.
 * Get the local APIC ID from the memory mapped APIC
 * and use it to locate the index to the parameter vector;
 * load the PDB with the page table address from the
 * information vector;
 * make an identity map for the inter-segment jump below,
 * using the stack space to hold a temporary PDP and PD;
 * enable and activate .long mode;
 * make an inter-segment jump to the .long mode code.
 */
.code32

/*
 * Macros for accessing page table entries; must turn
 * the C-style array-index macros into a page table byte
 * offset.
 */
#define PML4O(v)	((PTLX((v), 3))<<3)
#define PDPO(v)		((PTLX((v), 2))<<3)
#define PDO(v)		((PTLX((v), 1))<<3)
#define PTO(v)		((PTLX((v), 0))<<3)

_protected:
	mov	$0xfee00000, %ebp	/* apicbase */
	mov	0x20(%ebp), %ebp	/* Id */
	shr	$0x18, %ebp		/* becomes RARG later */

	mov	%ebp, %eax		/* apicno */
	imul	$0x20, %eax, %eax	/* [apicno] */
	mov	$_real, %ebx
	add	$0x1000, %ebx		/* sipi */
	add	%eax, %ebx		/* sipi[apicno] */

	mov	(%ebx), %esi		/* sipi[apicno].pml4 */

	mov	%esi, %eax
	mov	%eax, %cr3		/* load the mmu */

	mov	%eax, %edx
	sub	$MACHSTKSZ, %edx	/* PDP for identity map */
	add	$(PteRW|PteP), %edx
	mov	%edx, PML4O(0)(%eax)	/* PML4E for identity map */

	sub	$MACHSTKSZ, %eax	/* PDP for identity map */
	add	$PTSZ, %edx
	mov	%edx, PDPO(0)(%eax)	/* PDPE for identity map */
	mov	$(PtePS|PteRW|PteP), %edx
	add	$PTSZ, %eax		/* PD for identity map */
	mov	%edx, PDO(0)(%eax)	/* PDE for identity 0-[24]MiB */

/*
 * Enable and activate .long Mode. From the manual:
 * 	make sure Page Size Extentions are off, and Page Global
 *	Extensions and Physical Address Extensions are on in CR4;
 *	set .long Mode Enable in the Extended Feature Enable MSR;
 *	set Paging Enable in CR0;
 *	make an inter-segment jump to the .long Mode code.
 * It's all in 32-bit mode until the jump is made.
 */
_lme:
	mov	%cr4, %eax
	and	$~Pse, %eax		/* Page Size */
	or	$(Pge|Pae), %eax	/* Page Global, Phys. Address */
	mov	%eax, %cr4

	mov	$Efer, %ecx		/* Extended Feature Enable */
	rdmsr
	or	$Lme, %eax		/* .long Mode Enable */
	wrmsr

	mov	%cr0, %edx
	and	$~(Cd|Nw|Ts|Mp), %edx
	or	$(Pg|Wp), %edx		/* Paging Enable */
	mov	%edx, %cr0

	ljmp	$0x18, $_identity	/* 0x18 = SSEL(3, SsTIGDT|SsRPL0) */

/*
 * .long mode. Welcome to 2003.
 * Jump out of the identity map space;
 * load a proper .long mode GDT;
 * zap the identity map;
 * initialise the stack, RMACH, RUSER,
 * and call the C startup code.
 */
.code64
_identity:
	mov	$(_start64v+KZERO), %rax
	jmpq	*%rax

_start64v:
	mov	$_gdtptr64v, %rax
	lgdt	(%rax)

	xor	%rdx, %rdx		/* DX is 0 from here on */
	mov	%edx, %ds		/* not used in .long mode */
	mov	%edx, %es		/* not used in .long mode */
	mov	%edx, %fs
	mov	%edx, %gs
	mov	%edx, %ss		/* not used in .long mode */

	mov	%esi, %esi		/* sipi[apicno].pml4 */
	mov	%rsi, %rax
	add	$KZERO, %rax		/* PML4 */

	mov	%rdx, PML4O(0)(%rax)	/* zap identity map */
	mov	%rsi, %cr3		/* flush TLB */

	add	$KZERO, %rbx		/* &sipi[apicno] */

	mov	8(%rbx), %rsp		/* sipi[apicno].stack */

	push	%rdx			/* clear flags */
	popfq
	mov	%ebp, %ebp		/* APIC ID */
	push	%rbp			/* apicno */

	mov	16(%rbx), %r15		/* sipi[apicno].mach */
	mov	%rdx, %r14
	mov	24(%rbx), %rax		/* sipi[apicno].pc */
	callq	*%rax			/* (*sipi[apicno].pc)(apicno) */

_ndnr:
	jmp	_ndnr
	
.globl sipihandlerend
sipihandlerend:
	jmp	sipihandlerend
