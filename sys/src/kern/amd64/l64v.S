#include "amd64.h"

.code64

/*
 * Port I/O.
 */
.global inb
inb:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	XORL	%eax, %eax
	INB	%dx
	ret

.global insb
insb:
	movl	%edx, %ecx
	movl	%edi, %edx
	movq	%rsi, %rdi
	cld
	REP;	INSB
	ret

.global ins
ins:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	XORL	%eax, %eax
	INW	%dx
	ret

/*
 * void	inss(int32_t port, void* buf, int32_t size);
 */
.global inss
inss:
	movl	%edx, %ecx
	movl	%edi, %edx
	movq	%rsi, %rdi
	cld
	REP;	INSW
	ret

.global inl
inl:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	INL	%dx
	ret

.global insl
insl:
	movl	%edx, %ecx
	movl	%edi, %edx
	movq	%rsi, %rdi
	cld
	REP; INSL
	ret

.global outb
outb:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	movl	%esi, %eax
	OUTB	%dx
	ret

.global outsb
outsb:
	movl	%edx, %ecx
	movl	%edi, %edx
	movq	%rsi, %rdi
	cld
	REP; OUTSB
	ret

.global outs
outs:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	movl	%esi, %eax
	OUTW	%dx
	ret

.global outss
outss:
	movl	%edx, %ecx
	movl	%edi, %edx
	cld
	REP; OUTSW
	ret

.global outl
outl:
	movl	%edi, %edx			/* movl	port+0(FP), DX */
	movl	%esi, %eax
	OUTL	%dx
	ret

.global outsl
outsl:
	movl	%edx, %ecx
	movl	%edi, %edx
	movq	%rsi, %rdi
	cld
	REP; OUTSL
	ret

/*
 * Load/store segment descriptor tables:
 *	GDT - global descriptor table
 *	IDT - interrupt descriptor table
 *	TR - task register
 * GDTR and LDTR take an m16:m64 argument,
 * so shuffle the stack arguments to
 * get it in the right format.
 */
.global gdtget
gdtget:
	sgdt	(%rdi)		/* Note: 10 bytes returned */
	ret

.global lgdt
lgdt:
	lgdt	(%rdi)
	ret

.global lidt
lidt:
	lgdt	(%rdi)
	ret

// Called with the address of gdt in rdi.
// Load the gdt, then do a ret which will use the argument on the stack as
// a segment #. This stuff is just crazy.
// We have to push %rsi, then 16 bits(really!) of %rdi.
.global gdtput
gdtput:
	pushq	%rsi
	movq	%rdi, %rax
	pushw	%ax
	mov	%rsp, %rax
	lgdt	(%rax)
	popw 	%ax
	popq	%rax
	xorq	%rax, %rax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	popq	%rax
	pushq	%rdx
	pushq	%rax
	lretq

.global idtput
idtput:
	// save %rdi, since we are going to modify it.
	pushq	%rdi
	// Push the two quads onto the stack,
	// which arranges them in memory.
	pushq	%rsi
	shlq	$48, %rdi
	pushq	%rdi
	movq	%rsp, %rax
	addq	$6, %rax
	lidt	(%rax)
	popq	%rdi
	popq	%rsi
	popq	%rdi
	ret

.global trput
trput:
	ltr	%di
	ret

/*
 * Read/write various system registers.
 */
.global cr0get
cr0get:
	movq	%cr0, %rax
	ret

.global cr0put
cr0put:
	movq	%rdi, %rax
	movq	%rax, %cr0
	ret

.global cr2get
cr2get:
	movq	%cr2, %rax
	ret

.global cr3get
cr3get:
	movq	%cr3, %rax
	ret

.global cr3put
cr3put:
	movq	%rdi, %rax
	movq	%rax, %cr3
	ret

.global cr4get
cr4get:
	movq	%CR4, %rax
	ret

.global cr4put
cr4put:
	movq	%rdi, %rax
	movq	%rax, %CR4
	ret

.global read_bp
read_bp:
	movq	%rbp, %rax
	ret

.global rdtsc
rdtsc:
	RDTSC
						/* u64int rdtsc(void); */
	xchgl	%edx, %eax				/* swap lo/hi, zero-extend */
	SHLQ	$32, %rax				/* hi<<32 */
	ORQ	%rdx, %rax				/* (hi<<32)|lo */
	ret

/* int rdmsr(uint32_t reg, uint64_t* value); */
.global rdmsr
rdmsr:
	pushq	%rcx
	pushq	%rdx
	pushq	%rbp
	xorq	%rbp, %rbp
	movl	%edi, %ecx
.global _rdmsrinst
_rdmsrinst:
	rdmsr
	xchgl	%edx, %eax			/* swap lo/hi, zero-extend */
	shlq	$32, %rax			/* hi<<32 */
	orq	%rdx, %rax			/* (hi<<32)|lo */
	movq	%rax, (%rsi)			/* set value */
	movq	%rbp, %rax			/* %rbp set to -1 if traped */
	popq	%rbp
	popq	%rdx
	popq	%rcx
	ret

/*int wrmsr(uint32_t reg, uint64_t value) */
.global wrmsr
wrmsr:
	pushq	%rcx
	pushq	%rdx
	pushq	%rbp
	movl	%edi, %ecx
	movl	%esi, %eax
	movq	%rsi, %rdx
	shrq	$32, %rdx
	xorq	%rbp, %rbp
.global _wrmsrinst
_wrmsrinst:
	wrmsr
	movq	%rbp, %rax
	popq	%rbp
	popq	%rdx
	popq	%rcx
	ret

.global invlpg
invlpg:
	INVLPG	(%rdi)
	ret

.global wbinvd
wbinvd:
	WBINVD
	ret

/*
 * BIOS32.
 */
.global bios32call
bios32call:
	xorl	%eax, %eax
	incl	%eax
	ret

/*
 * Serialisation.
 */
.global lfence
lfence:
	lfence
	ret

.global mfence
mfence:
	mfence
	ret

.global sfence
sfence:
	sfence
	ret

/*
 *	x86 convention is to use %rbp as the frame pointer,
 *	so we just return that register
 */
.global stackframe
stackframe:
	movq	%rbp, %rax
	retq

/*
 *	disable interrupts,
 *	return old flags for splx()
 */
.global splhi
splhi:
_splhi:
	pushfq
	popq	%rax
	testq	$If, %rax	/* If - Interrupt Flag */
	jz	_alreadyhi
	movq	0(%rsp), %rdi
	movq	%rdi, 8(%r15)	/* callerpc to m->splpc */
_alreadyhi:
	cli
	ret

/*
 *	enable interrupts,
 *	return old flags for splx()
 */
.global spllo
spllo:
_spllo:
	pushfq
	popq	%rax
	testq	$If, %rax	/* If - Interrupt Flag */
	jnz	_alreadylo
	movq	$0, 8(%r15)	/* clear m->splpc */
_alreadylo:
	sti
	ret

/*
 *	undo splhi or spllo,
 *	%rdi has flags before splhi or spllo
 */
.global splx
splx:
	testq	$If, %rdi	/* If - Interrupt Flag */
	jnz	_spllo		/* If set: enable */
	jmp	_splhi		/* else: disable */

.global spldone
spldone:
	ret

.global islo
islo:
	pushfq
	popq	%rax
	andq	$If, %rax	/* If - Interrupt Flag */
	ret

.global infected_with_std
infected_with_std:
	pushfq
	popq	%rax
	andq	$Df, %rax	/* Df - Direction Flag */
	ret

.global disinfect_std
disinfect_std:
	cld
	ret

/*
 * Synchronisation
 */
.global ainc
ainc:
	movl	$1, %eax
	lock; xaddl %eax, (%rdi)
	addl	$1, %eax
	ret

.global adec
adec:
	movl	$-1, %eax
	lock; xaddl %eax, (%rdi)
	subl	$1, %eax
	ret

/*
 * Synchronisation
 */
.global ainc16
ainc16:
	mov	$1, %ax
	lock; xadd %ax, (%rdi)
	add	$1, %ax
	ret

.global adec16
adec16:
	mov	$-1, %ax
	lock; xadd %ax, (%rdi)
	sub	$1, %ax
	ret


/*
 * Semaphores rely on negative values for the counter,
 * and don't have the same overflow/underflow conditions
 * as ainc/adec.
 */
.global semainc
semainc:
	movl	$1, %eax
	lock; xaddl %eax, (%rdi)
	addl	$1, %eax
	ret

.global semadec
semadec:
	movl	$-1, %eax
	lock; xaddl %eax, (%rdi)
	subl	$1, %eax
	ret

.global tas32
tas32:
	movl	$0xdeaddead, %eax
	xchgl	%eax, (%rdi)			/*  */
	ret

.global fas64
fas64:
	movq	%rdi, %rax
	//lock; xchgq	%eax, (%rdi)			/*  */
	ret

// %rdi:&key, %sil:old, %dl:new
// int cas8(void* %rdi, uint8_t %sil, uint8_t %dl);
.global cas8
cas8:
	movb	%sil, %al
	lock; cmpxchgb %dl, (%rdi)
	movl	$1, %eax
	jnz	_cas8r0
_cas8r1:
	ret
_cas8r0:
	decl	%eax
	ret

// %rdi:&key, %esi:old, %edx:new
// int cas16(void* %rdi, uint16_t %esi, uint16_t %edx);
.global cas16
cas16:
	movw	%si, %ax
	lock; cmpxchgw %dx, (%rdi)
	movl	$1, %eax
	jnz	_cas16r0
_cas16r1:
	ret
_cas16r0:
	decl	%eax
	ret

// %rdi:&key, %esi:old, %edx:new
// int cas32(void* %rdi, uint32_t %esi, uint32_t %edx);
.global cas32
cas32:
	movl	%esi, %eax
	lock; cmpxchgl %edx, (%rdi)
	movl	$1, %eax
	jnz	_cas32r0
_cas32r1:
	ret
_cas32r0:
	decl	%eax
	ret

// %rdi:&key, %esi:old, %edx:new
// int cas64(void* %rdi, uint64_t %rsi, uint64_t %rdx);
.global cas64
cas64:
	movq	%rsi, %rax
	lock; cmpxchgq %rdx, (%rdi)
	movl	$1, %eax
	jnz	_cas64r0
_cas64r1:
	ret
_cas64r0:
	decl	%eax
	ret


.global xchg32
.global xchg32u
xchg32:
xchg32u:
	movl	%esi, %eax
	lock; xchgl	%eax, (%rdi)
	ret

.global xchgm
xchgm:
	movq	%rsi, %rax
	lock; xchgq	%rax, (%rdi)
	ret

/*
 * Label consists of a stack pointer and a programme counter
 * 0(%rdi) is the SP, 8(%rdi) is the PC
 */
.global gotolabel
gotolabel:
	movq	%rdi, %rax
	movq	0(%rdi), %rsp

	// Can't kill this quite yet.
	movq	(16+5*8)(%rdi), %rbp

	movq	8(%rax), %rax			/* put return PC on the stack */
						/* NOTE: replaces previous caller? */
	movq	%rax, (%rsp)
	movq	$1, %rax			/* return 1 */
	ret

/* save all registers on this stack, the save stack
 * in the label struct.
 */
.global slim_setlabel
slim_setlabel:
	// %rax is trashable.
	movq	0(%rsp), %rax			/* store return PC */
	movq	%rax, 8(%rdi)

	// Can't kill this quite yet.
	movq	%rbp, (16+5*8)(%rdi)

	movq	%rsp, 0(%rdi)	/* store SP */
	movl	$0, %eax	/* return 0 */
	ret

.global pause
pause:
	pause
	ret

.global halt
halt:
	cli
	cmpl	$0, nrdy
	je	_nothingready
	sti
	ret
_nothingready:
	sti
	HLT
	ret

.global hardhalt
hardhalt:
	sti
.global _halt
_halt:
	HLT
	ret

/*
 * uint32_t mwait32(void* %rdi, uint32_t %esi);
 */
.global mwait32
mwait32:
	cmpl	(%rdi), %esi			/* changed yet? */
	jne	_mm32done

	movq	%rdi, %rax			/* linear address to monitor */
	xorq	%rcx, %rcx			/* extensions */
	xorq	%rdx, %rdx			/* hints */
	monitor

	cmpl	(%rdi), %esi			/* changed yet? */
	jne	_mm32done

	/*xorq	CX, CX*/			/* extensions (different from monitor) */
	xorq	%rax, %rax			/* hints */
	mwait
_mm32done:
	movl	(%rdi), %eax
	ret

/* void	mwait(void*); */
.globl mwait
mwait:
	movq	%rdi, %rax
	movl	(%eax), %ecx
	orl	%ecx, %ecx
	jnz	_mwaitdone
	xorq	%rdx, %rdx
	monitor
	movl	(%eax), %ecx
	orl	%ecx, %ecx
	jnz	_mwaitdone
	xorq	%rax, %rax
	mwait
_mwaitdone:
	RET

rdrand32:
loop32:
	rdrand	%eax
	jc	loop32
	ret

rdrand64:
loop64:
	rdrand	%rax
	jc	loop64
	ret

.globl rdrandbuf
rdrandbuf:
	movq	%rdi, %rdx

	movl	%esi, %ecx
	shrq	$3, %rcx
eights:
	cmpl	$0, %ecx
	jg	f1
	call	rdrand64
	movq	%rax, 0(%rdx)
	addq	$8, %rdx
	subl	$1, %ecx
	jmp	eights

f1:
	movl	%esi, %ecx
	andl	$7, %ecx
	shrq	$2, %rcx
fours:
	cmpl	$0, %ecx
	jg	f2
	call	rdrand32
	movl	%eax, 0(%rdx)
	addq	$4, %rdx
	subl	$1, %ecx
	jmp	fours

f2:
	movl	%esi, %ecx
	andl	$3, %ecx
ones:
	cmpl	$0, %ecx
	jg	f3
	call	rdrand32
	movb	%al, 0(%rdx)
	addq	$1, %rdx
	subl	$1, %ecx
	jmp	ones

f3:
	RET


/*
 * Park a processor. Should never fall through a return from main to here,
 * should only be called by application processors when shutting down.
 */
.global idle
idle:
_idle:
	STI
	HLT
	JMP	_idle
