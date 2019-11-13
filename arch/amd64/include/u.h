/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#define nil			((void*)0)
typedef unsigned char		uint8_t;
typedef signed char		int8_t;
typedef unsigned short		uint16_t;
typedef signed short		int16_t;
typedef unsigned int		uint32_t;
typedef unsigned int		uint;
typedef signed int		int32_t;
typedef unsigned long long	uint64_t;
typedef long long		int64_t;
typedef uint64_t		uintptr;
typedef uint64_t		uintptr_t;
typedef uint32_t		usize;
typedef __SIZE_TYPE__		size_t;
typedef long			ssize_t;
typedef int32_t			pid_t;
typedef uint32_t		Rune;
typedef union FPdbleword	FPdbleword;
typedef uintptr_t		jmp_buf[10]; // for registers.
typedef long			off_t;
typedef long			ptrdiff_t;

#define JMPBUFSP 6
#define JMPBUFPC 7
#define JMPBUFARG1 8
#define JMPBUFARG2 9

#define JMPBUFDPC 0
typedef unsigned int mpdigit; /* for /sys/include/mp.h */

/* MXCSR */
/* fcr */
#define FPFTZ (1<<15)	/* amd64 */
#define FPINEX (1<<12)
#define FPUNFL (1<<11)
#define FPOVFL (1<<10)
#define FPZDIV (1<<9)
#define FPDNRM (1<<8)	/* amd64 */
#define FPINVAL (1<<7)
#define FPDAZ (1<<6)	/* amd64 */
#define FPRNR (0<<13)
#define FPRZ (3<<13)
#define FPRPINF (2<<13)
#define FPRNINF (1<<13)
#define FPRMASK (3<<13)
#define FPPEXT 0
#define FPPSGL 0
#define FPPDBL 0
#define FPPMASK 0
/* fsr */
#define FPAINEX (1<<5)
#define FPAUNFL (1<<4)
#define FPAOVFL (1<<3)
#define FPAZDIV (1<<2)
#define FPADNRM (1<<1)	/* not in plan 9 */
#define FPAINVAL (1<<0)
union FPdbleword
{
 double x;
 struct { /* little endian */
  uint lo;
  uint hi;
 };
};

typedef union FdPair
{
	int fd[2];
	long aslong;
} FdPair;

typedef __builtin_va_list va_list;

#define va_start(v,l)	__builtin_va_start(v,l)
#define va_end(v)	__builtin_va_end(v)
#define va_arg(v,l)	__builtin_va_arg(v,l)
#define va_copy(v,l)	__builtin_va_copy(v,l)

typedef union NativeTypes
{
	volatile char c;
	volatile unsigned char uc;
	volatile short s;
	volatile unsigned short us;
	volatile int i;
	volatile unsigned int ui;
	volatile long l;
	volatile unsigned long ul;
	void* p;
} NativeTypes;
extern volatile NativeTypes* _sysargs;
# include "syscalls.h"
