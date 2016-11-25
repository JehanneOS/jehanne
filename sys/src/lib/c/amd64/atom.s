.text

.globl ainc				/* long ainc(long *); */
					/* N.B.: long in Plan 9 is 32 BITS! */
ainc:

	pushq	%rcx
ainclp:
	movl	(%rdi), %eax
	movl	%eax, %ecx
	incl	%ecx		/* new */
	lock; cmpxchgl %ecx, (%rdi)
	jnz	ainclp
	movl	%ecx, %eax
	popq %rcx
	ret

.globl adec     /* long adec(long*); */
adec:
	pushq	%rcx
adeclp:
	movl	(%rdi), %eax
	movl	%eax, %ecx
	decl	%ecx		/* new */
	lock; cmpxchgl %ecx, (%rdi)
	jnz	adeclp
	movl	%ecx, %eax
	popq %rcx
	ret

/*
 * void mfence(void);
 */
.globl mfence
mfence:
	mfence
	ret

