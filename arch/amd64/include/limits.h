/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016-2019 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _JEHANNE_LIMITS_H
#define _JEHANNE_LIMITS_H

#undef CHAR_BIT
#define CHAR_BIT __CHAR_BIT__

#ifndef MB_LEN_MAX
#define MB_LEN_MAX 4 /* UTFmax in libc.h */
#endif

#define SCHAR_MAX 127
#define SCHAR_MIN (-128)

#define UCHAR_MAX (SCHAR_MAX * 2 + 1)

/* Jehanne's chars are signed */
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX

#define SHRT_MIN	(-32768)
#define SHRT_MAX	32767

#define USHRT_MAX	65535

#define INT_MIN	(-INT_MAX - 1)
#define INT_MAX	2147483647

#define UINT_MAX	4294967295U

#define LONG_MAX	9223372036854775807L
#define LONG_MIN	(-LONG_MAX - 1L)

#define ULONG_MAX	18446744073709551615UL

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

/* Minimum and maximum values a `signed long long int' can hold.  */
#define LLONG_MAX	9223372036854775807LL
#define LLONG_MIN	(-LLONG_MAX - 1LL)

/* Maximum value an `unsigned long long int' can hold.  (Minimum is 0.)  */
#   define ULLONG_MAX	18446744073709551615ULL

#  endif /* ISO C99 */

#if defined (__GNU_LIBRARY__) ? defined (__USE_GNU) : !defined (__STRICT_ANSI__)
/* Minimum and maximum values a `signed long long int' can hold.  */
# undef LONG_LONG_MIN
# define LONG_LONG_MIN (-LONG_LONG_MAX - 1LL)
# undef LONG_LONG_MAX
# define LONG_LONG_MAX 9223372036854775807L

/* Maximum value an `unsigned long long int' can hold.  (Minimum is 0).  */
# undef ULONG_LONG_MAX
# define ULONG_LONG_MAX (LONG_LONG_MAX * 2ULL + 1ULL)
#endif

#ifdef __STDC_WANT_IEC_60559_BFP_EXT__
/* TS 18661-1 widths of integer types.  */
# undef CHAR_WIDTH
# define CHAR_WIDTH 8
# undef SCHAR_WIDTH
# define SCHAR_WIDTH 8
# undef UCHAR_WIDTH
# define UCHAR_WIDTH 8
# undef SHRT_WIDTH
# define SHRT_WIDTH 16
# undef USHRT_WIDTH
# define USHRT_WIDTH 16
# undef INT_WIDTH
# define INT_WIDTH 32
# undef UINT_WIDTH
# define UINT_WIDTH 32
# undef LONG_WIDTH
# define LONG_WIDTH 64
# undef ULONG_WIDTH
# define ULONG_WIDTH 64
# undef LLONG_WIDTH
# define LLONG_WIDTH 64
# undef ULLONG_WIDTH
# define ULLONG_WIDTH 64
#endif

#endif /* _LIMITS_H___ */
