/*
 * The kernel and the libc use the same constant for TAS
 */
.text
.globl jehanne__tas
jehanne__tas:
	movl    $0xdeaddead, %eax
	xchgl   %eax, 0(%rdi)            /* lock->key */
	ret

