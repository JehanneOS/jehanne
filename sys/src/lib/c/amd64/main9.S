#define NPRIVATES 16

.text

// initially %rdi contains the process pid;
// here %rdi and %rsi are set up as argc, argv for main.
.globl	_main
_main:

	movl	%r12d, _mainpid
	movq	0(%rsp), %rdi
	leaq	8(%rsp), %rsi
	movq 	$-128, _privates
	addq	%rsp, _privates
	movl 	$NPRIVATES, _nprivates
	leaq	-160(%rsp), %rsp
	call	main

loop:
	movq	$_exits, %rdi
	call	exits
	jmp	loop

.data
_exits:
	.ascii	"main"
