/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _APW_LIMITS_H
#define _APW_LIMITS_H
#ifndef HIDE_JEHANNE_APW

#undef CHAR_BIT
#define CHAR_BIT __CHAR_BIT__

#ifndef MB_LEN_MAX
#define MB_LEN_MAX 4 /* UTFmax in libc.h */
#endif

#undef SCHAR_MIN
#define SCHAR_MIN (-SCHAR_MAX - 1)
#undef SCHAR_MAX
#define SCHAR_MAX __SCHAR_MAX__

#undef UCHAR_MAX
#define UCHAR_MAX (SCHAR_MAX * 2 + 1)

#ifdef __CHAR_UNSIGNED__
# undef CHAR_MIN
# define CHAR_MIN 0
# undef CHAR_MAX
# define CHAR_MAX UCHAR_MAX
#else
# undef CHAR_MIN
# define CHAR_MIN SCHAR_MIN
# undef CHAR_MAX
# define CHAR_MAX SCHAR_MAX
#endif

#undef SHRT_MIN
#define SHRT_MIN (-SHRT_MAX - 1)
#undef SHRT_MAX
#define SHRT_MAX __SHRT_MAX__

#undef USHRT_MAX
#define USHRT_MAX (SHRT_MAX * 2 + 1)

#undef INT_MIN
#define INT_MIN (-INT_MAX - 1)
#undef INT_MAX
#define INT_MAX __INT_MAX__

#undef UINT_MAX
#define UINT_MAX (INT_MAX * 2U + 1U)

#undef LONG_MIN
#define LONG_MIN (-LONG_MAX - 1L)
#undef LONG_MAX
#define LONG_MAX __LONG_MAX__

#undef ULONG_MAX
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
# undef LLONG_MIN
# define LLONG_MIN (-LLONG_MAX - 1LL)
# undef LLONG_MAX
# define LLONG_MAX __LONG_LONG_MAX__

# undef ULLONG_MAX
# define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#endif

#if defined (__GNU_LIBRARY__) ? defined (__USE_GNU) : !defined (__STRICT_ANSI__)
# undef LONG_LONG_MIN
# define LONG_LONG_MIN (-LONG_LONG_MAX - 1LL)
# undef LONG_LONG_MAX
# define LONG_LONG_MAX __LONG_LONG_MAX__

# undef ULONG_LONG_MAX
# define ULONG_LONG_MAX (LONG_LONG_MAX * 2ULL + 1ULL)
#endif

#ifdef __STDC_WANT_IEC_60559_BFP_EXT__
# undef CHAR_WIDTH
# define CHAR_WIDTH __SCHAR_WIDTH__
# undef SCHAR_WIDTH
# define SCHAR_WIDTH __SCHAR_WIDTH__
# undef UCHAR_WIDTH
# define UCHAR_WIDTH __SCHAR_WIDTH__
# undef SHRT_WIDTH
# define SHRT_WIDTH __SHRT_WIDTH__
# undef USHRT_WIDTH
# define USHRT_WIDTH __SHRT_WIDTH__
# undef INT_WIDTH
# define INT_WIDTH __INT_WIDTH__
# undef UINT_WIDTH
# define UINT_WIDTH __INT_WIDTH__
# undef LONG_WIDTH
# define LONG_WIDTH __LONG_WIDTH__
# undef ULONG_WIDTH
# define ULONG_WIDTH __LONG_WIDTH__
# undef LLONG_WIDTH
# define LLONG_WIDTH __LONG_LONG_WIDTH__
# undef ULLONG_WIDTH
# define ULLONG_WIDTH __LONG_LONG_WIDTH__
#endif

#endif
#endif
