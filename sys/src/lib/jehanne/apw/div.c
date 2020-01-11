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
#include <apw/stdlib.h>

div_t
div(int num, int denom)
{
	div_t r;

	r.quot = num / denom;
	r.rem = num % denom;
	if (num >= 0 && r.rem < 0) {
		++r.quot;
		r.rem -= denom;
	}
	else if (num < 0 && r.rem > 0) {
		--r.quot;
		r.rem += denom;
	}
	return (r);
}


ldiv_t
ldiv(long num, long denom)
{
	ldiv_t r;

	r.quot = num / denom;
	r.rem = num % denom;
	if (num >= 0 && r.rem < 0) {
		++r.quot;
		r.rem -= denom;
	}
	else if (num < 0 && r.rem > 0) {
		--r.quot;
		r.rem += denom;
	}
	return (r);
}
