/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
DSApriv*getdsakey(int argc, char **argv, int needprivate, Attr **pa);
RSApriv*getrsakey(int argc, char **argv, int needprivate, Attr **pa);
uint8_t*	put4(uint8_t *p, uint32_t n);
uint8_t*	putmp2(uint8_t *p, mpint *b);
uint8_t*	putn(uint8_t *p, void *v, uint32_t n);
uint8_t*	putstr(uint8_t *p, char *s);
