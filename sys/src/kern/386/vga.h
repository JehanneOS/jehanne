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
typedef struct VGAreg VGAreg;
struct VGAreg
{
	uint32_t	di;		/* general registers */
	uint32_t	si;		/* ... */
	uint32_t	bp;		/* ... */
	uint32_t	nsp;
	uint32_t	bx;		/* ... */
	uint32_t	dx;		/* ... */
	uint32_t	cx;		/* ... */
	uint32_t	ax;		/* ... */
	uint32_t	gs;		/* data segments */
	uint32_t	fs;		/* ... */
	uint32_t	es;		/* ... */
	uint32_t	ds;		/* ... */
	uint32_t	trap;		/* trap type */
	uint32_t	ecode;		/* error code (or zero) */
	uint32_t	pc;		/* pc */
	uint32_t	cs;		/* old context */
	uint32_t	flags;		/* old flags */
	union {
		uint32_t	usp;
		uint32_t	sp;
	};
	uint32_t	ss;		/* old stack segment */
};
