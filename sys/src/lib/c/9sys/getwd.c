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

#include <u.h>
#include <libc.h>

/* Fills buf with the current workking directory.
 *
 * Returns
 * - 0 on error
 * - the negated length of the working directory path if nbuf is too small
 * - the (positive) length of the working directory on success
 */
long
jehanne_getwd(char *buf, int nbuf)
{
	long n;
	int fd;

	fd = open("#0/wdir", OREAD);
	if(fd < 0)
		return 0;
	n = read(fd, nil, -1);
	if(n == ~0)	/* an error occurred */
		return 0;
	if(nbuf >= ~n)
		n = read(fd, buf, nbuf);
	close(fd);
	return n;
}
