#include "mem.h"
#include "amd64.h"
#ifndef __ASSEMBLER__
#define __ASSEMBLER__
#endif

/*
 * starting the user program up. First time.
 */
.globl touser
touser:
	cli
	swapgs

	movq	$0, %r15
	movq	$0, %r14

	movq	$SSEL(SiUDS, SsRPL3), %rax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs

	movq	$(UTZERO+0x20), %rcx		/* ip */
	movq	$If, %r11			/* flags */

	movq	%rdi, %rsp			/* sp */

	sysretq

.globl syscallentry
syscallentry:
	swapgs

	xchgq	%gs:0, %r15
	movq	%r14, 24(%r15)			/* save %r14 to m->tmp0 */
	movq	16(%r15), %r14			/* m->proc (set up->) */
	movq	%r13, 32(%r15)			/* save %r13 to m->tmp1 */
	movq	16*8(%r14), %r13		/* m->proc->kstack (after the Label) */
	xchgq	%r13, %rsp
	movq	24(%r15), %r14			/* restore %r14 */

	addq	$KSTACK, %rsp

	/* build Ureg */
	pushq	$UDSEL				/* old stack segment */
	pushq	%r13				/* old sp */
	pushq	%r11				/* old flags */
	pushq	$UESEL				/* old code segment */
	pushq	%rcx				/* old ip */

	movq	32(%r15), %r13			/* restore %r13 from m->tmp1 */
	xchgq	%gs:0, %r15			/* restore %r15 */

	subq	$(17*8), %rsp

	movq	%rax, (0*8)(%rsp)	// ureg.ax
	movq	%rbx, (1*8)(%rsp)	// ureg.bx
	movq	%rcx, (2*8)(%rsp)	// ureg.cx
	movq	%rdx, (3*8)(%rsp)	// ureg.dx
	movq	%rsi, (4*8)(%rsp)	// ...
	movq	%rdi, (5*8)(%rsp)
	movq	%rbp, (6*8)(%rsp)
	movq	%r8, (7*8)(%rsp)
	movq	%r9, (8*8)(%rsp)
	movq	%r10, (9*8)(%rsp)
	movq	%r11, (10*8)(%rsp)
	movq	%r12, (11*8)(%rsp)
	movq	%r13, (12*8)(%rsp)
	movq	%r14, (13*8)(%rsp)
	movq	%r15, (14*8)(%rsp)
	// (15*8)(%rsp)	// ureg.type
	// (16*8)(%rsp) // ureg.error

	/* prepare m-> and up-> for the kernel */
	movq	%gs:0, %r15
	movq	16(%r15), %r14

	movq	%rsp, %rsi 				/* Ureg* */
	// system call number is in %rax, as per linux.
	movq	%rax, %rdi
	xorq	%rax, %rax
	pushq	%rax
	popfq			/* clear all flags. is there something else we should clear too? */
	movq	$0, %rbp	/* stack traces end here */
	call	syscall

.globl	syscallreturn
syscallreturn:

	// restore from ureg
	movq	(0*8)(%rsp),%rax
	movq	(1*8)(%rsp),%rbx
	movq	(2*8)(%rsp),%rcx
	movq	(3*8)(%rsp),%rdx
	movq	(4*8)(%rsp),%rsi
	movq	(5*8)(%rsp),%rdi
	movq	(6*8)(%rsp),%rbp
	movq	(7*8)(%rsp),%r8
	movq	(8*8)(%rsp),%r9
	movq	(9*8)(%rsp),%r10
	movq	(10*8)(%rsp),%r11
	movq	(11*8)(%rsp),%r12
	movq	(12*8)(%rsp),%r13
	movq	(13*8)(%rsp),%r14
	movq	(14*8)(%rsp),%r15

	addq	$(17*8), %rsp			/* registers + arguments */

	cli
	swapgs

	movq	0(%rsp), %rcx			/* ip */
	movq	16(%rsp), %r11			/* flags */
	movq	24(%rsp), %rsp			/* sp */

	sysretq

.globl sysrforkret
sysrforkret:
	movq	$0, 0(%rsp)
//	movq	%r14, (13*8)(%rsp)		/* preserve up-> */
//	movq	%r15, (14*8)(%rsp)		/* preserve m-> */
	jmp	syscallreturn
