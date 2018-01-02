/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

void
hnputv(void *p, uint64_t v)
{
	uint8_t *a;

	a = p;
	hnputl(a, v>>32);
	hnputl(a+4, v);
}

void
hnputl(void *p, uint32_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v>>24;
	a[1] = v>>16;
	a[2] = v>>8;
	a[3] = v;
}

void
hnputs(void *p, uint16_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v>>8;
	a[1] = v;
}

uint64_t
nhgetv(void *p)
{
	uint8_t *a;

	a = p;
	return ((int64_t)nhgetl(a) << 32) | nhgetl(a+4);
}

uint32_t
nhgetl(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0]<<24)|(a[1]<<16)|(a[2]<<8)|(a[3]<<0);
}

uint16_t
nhgets(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0]<<8)|(a[1]<<0);
}
