/*
 * The CPUID instruction is always supported on the amd64.
 */
.globl cpuid
cpuid:
	pushq	%rbx
	pushq	%rcx
	pushq	%rdx
	movl	%edi, %eax
	cpuid
	movl	%eax, 0(%rsi)
	movl	%ebx, 4(%rsi)
	movl	%ecx, 8(%rsi)
	movl	%edx, 12(%rsi)
	popq	%rdx
	popq	%rcx
	popq	%rbx
	ret

/*
 * Basic timing loop to determine CPU frequency.
 * The AAM instruction is not available in 64-bit mode.
 */
.globl aamloop
aamloop:
	pushq	%rcx
	movq	%rdi, %rcx
aaml1:
	xorq	%rax, %rax				/* close enough */
	loop	aaml1
	popq	%rcx
	ret
