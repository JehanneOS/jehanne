/*
 * Interrupt/exception handling.
 */
#include "amd64.h"

.code64

.global _acintrp
_acintrp:
	pushq	%rax // bogus error code. Makes stack look like intre.
	// Except in this case, bogus error code is at 0(%rsp)
	// vno pointer is at 8(%rsp)
	movq	8(%rsp), %rax
	// Now %rax points to the vector number.
	jmp	_acintrcommon

.globl _acintre
_acintre:
	// The error code has been pushed. How I love the x86.
	// So the error code is ABOVE the pointer to the vector #.
	xchgq	%rax, (%rsp)

_acintrcommon:
		// Get the vector number into %al
	movb	(%rax), %al
	andq	$0xff, %rax
	// Put that at TOS (this is plan 9 argument style)
	xchgq	%rax, 0(%rsp)
	// 0(%rsp) now has the vno
	cmpw	$SSEL(SiCS, SsTIGDT|SsRPL0), 24(%rsp) 	/* old CS */
	je	_acintrnested

	swapgs

_acintrnested:
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%r11
	pushq	%r10
	pushq	%r9
	pushq	%r8
	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%rdx
	pushq	%rcx
	pushq	%rbx
	pushq	%rax

	movq	%rsp, %rdi // it's ok, we saved %rdi.
	call	trap
.globl _acintrr
_acintrr:
	popq	%rax
	popq	%rbx
	popq	%rcx
	popq	%rdx
	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%r8
	popq	%r9
	popq	%r10
	popq	%r11
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15

	cmpw	$SSEL(SiCS, SsTIGDT|SsRPL0), 24(%rsp) 	/* old CS */
	je	_aciretnested

	swapgs

_aciretnested:
	// Throw away:
	// The %rax you pushed (error code)
	// EIP from the vector table.
	addq	$16, %rsp
	iretq

.globl acidthandlers
acidthandlers:
	call _acintrp; .byte IdtDE		/* #DE Divide-by-Zero Error */
	call _acintrp; .byte IdtDB		/* #DB Debug */
	call _acintrp; .byte IdtNMI		/* #NMI Borked */
	call _acintrp; .byte IdtBP		/* #BP Breakpoint */
	call _acintrp; .byte IdtOF		/* #OF Overflow */
	call _acintrp; .byte IdtBR		/* #BR Bound-Range */
	call _acintrp; .byte IdtUD		/* #UD Invalid-Opcode */
	call _acintrp; .byte IdtNM		/* #NM Device-Not-Available */
	call _acintre; .byte IdtDF		/* #DF Double-Fault */
	call _acintrp; .byte Idt09		/* reserved */
	call _acintre; .byte IdtTS		/* #TS Invalid-TSS */
	call _acintre; .byte IdtNP		/* #NP Segment-Not-Present */
	call _acintre; .byte IdtSS		/* #SS Stack */
	call _acintre; .byte IdtGP		/* #GP General-Protection */
	call _acintre; .byte IdtPF		/* #PF Page-Fault */
	call _acintrp; .byte Idt0F		/* reserved */
	call _acintrp; .byte IdtMF		/* #MF x87 FPE-Pending */
	call _acintre; .byte IdtAC		/* #AC Alignment-Check */
	call _acintrp; .byte IdtMC		/* #MC Machine-Check */
	call _acintrp; .byte IdtXF		/* #XF SIMD Floating-Point */
	call _acintrp; .byte 0x14		/* reserved */
	call _acintrp; .byte 0x15		/* reserved */
	call _acintrp; .byte 0x16		/* reserved */
	call _acintrp; .byte 0x17		/* reserved */
	call _acintrp; .byte 0x18		/* reserved */
	call _acintrp; .byte 0x19		/* reserved */
	call _acintrp; .byte 0x1a		/* reserved */
	call _acintrp; .byte 0x1b		/* reserved */
	call _acintrp; .byte 0x1c		/* reserved */
	call _acintrp; .byte 0x1d		/* reserved */
	call _acintrp; .byte 0x1e		/* reserved */
	call _acintrp; .byte 0x1f		/* reserved */
	call _acintrp; .byte 0x20
	call _acintrp; .byte 0x21
	call _acintrp; .byte 0x22
	call _acintrp; .byte 0x23
	call _acintrp; .byte 0x24
	call _acintrp; .byte 0x25
	call _acintrp; .byte 0x26
	call _acintrp; .byte 0x27
	call _acintrp; .byte 0x28
	call _acintrp; .byte 0x29
	call _acintrp; .byte 0x2a
	call _acintrp; .byte 0x2b
	call _acintrp; .byte 0x2c
	call _acintrp; .byte 0x2d
	call _acintrp; .byte 0x2e
	call _acintrp; .byte 0x2f
	call _acintrp; .byte 0x30
	call _acintrp; .byte 0x31
	call _acintrp; .byte 0x32
	call _acintrp; .byte 0x33
	call _acintrp; .byte 0x34
	call _acintrp; .byte 0x35
	call _acintrp; .byte 0x36
	call _acintrp; .byte 0x37
	call _acintrp; .byte 0x38
	call _acintrp; .byte 0x39
	call _acintrp; .byte 0x3a
	call _acintrp; .byte 0x3b
	call _acintrp; .byte 0x3c
	call _acintrp; .byte 0x3d
	call _acintrp; .byte 0x3e
	call _acintrp; .byte 0x3f
	call _acintrp; .byte 0x40
	call _acintrp; .byte 0x41
	call _acintrp; .byte 0x42
	call _acintrp; .byte 0x43
	call _acintrp; .byte 0x44
	call _acintrp; .byte 0x45
	call _acintrp; .byte 0x46
	call _acintrp; .byte 0x47
	call _acintrp; .byte 0x48
	call _acintrp; .byte 0x49
	call _acintrp; .byte 0x4a
	call _acintrp; .byte 0x4b
	call _acintrp; .byte 0x4c
	call _acintrp; .byte 0x4d
	call _acintrp; .byte 0x4e
	call _acintrp; .byte 0x4f
	call _acintrp; .byte 0x50
	call _acintrp; .byte 0x51
	call _acintrp; .byte 0x52
	call _acintrp; .byte 0x53
	call _acintrp; .byte 0x54
	call _acintrp; .byte 0x55
	call _acintrp; .byte 0x56
	call _acintrp; .byte 0x57
	call _acintrp; .byte 0x58
	call _acintrp; .byte 0x59
	call _acintrp; .byte 0x5a
	call _acintrp; .byte 0x5b
	call _acintrp; .byte 0x5c
	call _acintrp; .byte 0x5d
	call _acintrp; .byte 0x5e
	call _acintrp; .byte 0x5f
	call _acintrp; .byte 0x60
	call _acintrp; .byte 0x61
	call _acintrp; .byte 0x62
	call _acintrp; .byte 0x63
	call _acintrp; .byte 0x64
	call _acintrp; .byte 0x65
	call _acintrp; .byte 0x66
	call _acintrp; .byte 0x67
	call _acintrp; .byte 0x68
	call _acintrp; .byte 0x69
	call _acintrp; .byte 0x6a
	call _acintrp; .byte 0x6b
	call _acintrp; .byte 0x6c
	call _acintrp; .byte 0x6d
	call _acintrp; .byte 0x6e
	call _acintrp; .byte 0x6f
	call _acintrp; .byte 0x70
	call _acintrp; .byte 0x71
	call _acintrp; .byte 0x72
	call _acintrp; .byte 0x73
	call _acintrp; .byte 0x74
	call _acintrp; .byte 0x75
	call _acintrp; .byte 0x76
	call _acintrp; .byte 0x77
	call _acintrp; .byte 0x78
	call _acintrp; .byte 0x79
	call _acintrp; .byte 0x7a
	call _acintrp; .byte 0x7b
	call _acintrp; .byte 0x7c
	call _acintrp; .byte 0x7d
	call _acintrp; .byte 0x7e
	call _acintrp; .byte 0x7f
	call _acintrp; .byte 0x80
	call _acintrp; .byte 0x81
	call _acintrp; .byte 0x82
	call _acintrp; .byte 0x83
	call _acintrp; .byte 0x84
	call _acintrp; .byte 0x85
	call _acintrp; .byte 0x86
	call _acintrp; .byte 0x87
	call _acintrp; .byte 0x88
	call _acintrp; .byte 0x89
	call _acintrp; .byte 0x8a
	call _acintrp; .byte 0x8b
	call _acintrp; .byte 0x8c
	call _acintrp; .byte 0x8d
	call _acintrp; .byte 0x8e
	call _acintrp; .byte 0x8f
	call _acintrp; .byte 0x90
	call _acintrp; .byte 0x91
	call _acintrp; .byte 0x92
	call _acintrp; .byte 0x93
	call _acintrp; .byte 0x94
	call _acintrp; .byte 0x95
	call _acintrp; .byte 0x96
	call _acintrp; .byte 0x97
	call _acintrp; .byte 0x98
	call _acintrp; .byte 0x99
	call _acintrp; .byte 0x9a
	call _acintrp; .byte 0x9b
	call _acintrp; .byte 0x9c
	call _acintrp; .byte 0x9d
	call _acintrp; .byte 0x9e
	call _acintrp; .byte 0x9f
	call _acintrp; .byte 0xa0
	call _acintrp; .byte 0xa1
	call _acintrp; .byte 0xa2
	call _acintrp; .byte 0xa3
	call _acintrp; .byte 0xa4
	call _acintrp; .byte 0xa5
	call _acintrp; .byte 0xa6
	call _acintrp; .byte 0xa7
	call _acintrp; .byte 0xa8
	call _acintrp; .byte 0xa9
	call _acintrp; .byte 0xaa
	call _acintrp; .byte 0xab
	call _acintrp; .byte 0xac
	call _acintrp; .byte 0xad
	call _acintrp; .byte 0xae
	call _acintrp; .byte 0xaf
	call _acintrp; .byte 0xb0
	call _acintrp; .byte 0xb1
	call _acintrp; .byte 0xb2
	call _acintrp; .byte 0xb3
	call _acintrp; .byte 0xb4
	call _acintrp; .byte 0xb5
	call _acintrp; .byte 0xb6
	call _acintrp; .byte 0xb7
	call _acintrp; .byte 0xb8
	call _acintrp; .byte 0xb9
	call _acintrp; .byte 0xba
	call _acintrp; .byte 0xbb
	call _acintrp; .byte 0xbc
	call _acintrp; .byte 0xbd
	call _acintrp; .byte 0xbe
	call _acintrp; .byte 0xbf
	call _acintrp; .byte 0xc0
	call _acintrp; .byte 0xc1
	call _acintrp; .byte 0xc2
	call _acintrp; .byte 0xc3
	call _acintrp; .byte 0xc4
	call _acintrp; .byte 0xc5
	call _acintrp; .byte 0xc6
	call _acintrp; .byte 0xc7
	call _acintrp; .byte 0xc8
	call _acintrp; .byte 0xc9
	call _acintrp; .byte 0xca
	call _acintrp; .byte 0xcb
	call _acintrp; .byte 0xcc
	call _acintrp; .byte 0xce
	call _acintrp; .byte 0xce
	call _acintrp; .byte 0xcf
	call _acintrp; .byte 0xd0
	call _acintrp; .byte 0xd1
	call _acintrp; .byte 0xd2
	call _acintrp; .byte 0xd3
	call _acintrp; .byte 0xd4
	call _acintrp; .byte 0xd5
	call _acintrp; .byte 0xd6
	call _acintrp; .byte 0xd7
	call _acintrp; .byte 0xd8
	call _acintrp; .byte 0xd9
	call _acintrp; .byte 0xda
	call _acintrp; .byte 0xdb
	call _acintrp; .byte 0xdc
	call _acintrp; .byte 0xdd
	call _acintrp; .byte 0xde
	call _acintrp; .byte 0xdf
	call _acintrp; .byte 0xe0
	call _acintrp; .byte 0xe1
	call _acintrp; .byte 0xe2
	call _acintrp; .byte 0xe3
	call _acintrp; .byte 0xe4
	call _acintrp; .byte 0xe5
	call _acintrp; .byte 0xe6
	call _acintrp; .byte 0xe7
	call _acintrp; .byte 0xe8
	call _acintrp; .byte 0xe9
	call _acintrp; .byte 0xea
	call _acintrp; .byte 0xeb
	call _acintrp; .byte 0xec
	call _acintrp; .byte 0xed
	call _acintrp; .byte 0xee
	call _acintrp; .byte 0xef
	call _acintrp; .byte 0xf0
	call _acintrp; .byte 0xf1
	call _acintrp; .byte 0xf2
	call _acintrp; .byte 0xf3
	call _acintrp; .byte 0xf4
	call _acintrp; .byte 0xf5
	call _acintrp; .byte 0xf6
	call _acintrp; .byte 0xf7
	call _acintrp; .byte 0xf8
	call _acintrp; .byte 0xf9
	call _acintrp; .byte 0xfa
	call _acintrp; .byte 0xfb
	call _acintrp; .byte 0xfc
	call _acintrp; .byte 0xfd
	call _acintrp; .byte 0xfe
	call _acintrp; .byte 0xff
