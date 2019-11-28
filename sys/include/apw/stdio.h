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
#ifndef _APW_STDIO_H
#define _APW_STDIO_H
#ifndef HIDE_JEHANNE_APW

/* Simple trick to avoid name clash between native and standard headers:
 *
 * when you use stdio.h in native Jehanne code, you have to include it
 * after u.h and libc.h: thus in native code, JEHANNE_LIBC will already
 * be defined, while in posix code it will not (and it will undef at
 * the first includsion of the wrapper)
 *
 * This way, defining JEHANNE_LIBC is all we have to do in libc.h to
 * cope with ANSI/POSIX pollution, without introducing ifdef guards.
 */
#ifndef JEHANNE_LIBC
#include "libc.wrapper.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "../stdio.h"
#ifdef __cplusplus
}
#endif
#endif
#endif
