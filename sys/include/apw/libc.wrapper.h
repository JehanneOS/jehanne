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

/* This wrapper protect Jehanne default headers from multiple
 * inclusions in POSIX source code.
 *
 * As far as u.h and libc.h are not standard headers in POSIX, a simple
 * guarded inclusion will go to the appropriate folders /arch/amd64/include
 * and /sys/include
 */
#ifndef _JEHANNE_LIBC_WRAPPER
#define _JEHANNE_LIBC_WRAPPER
#ifdef __cplusplus
extern "C" {
#endif
#include <u.h>
#include "../libc.h"
#undef JEHANNE_LIBC
#ifdef __cplusplus
}
#endif
#endif
