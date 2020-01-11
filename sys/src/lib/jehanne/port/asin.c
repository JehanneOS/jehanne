/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * jehanne_asin(arg) and jehanne_acos(arg) return the arcsin, arccos,
 * respectively of their arguments.
 *
 * Arctan is called after appropriate range reduction.
 */

#include <u.h>
#include <libc.h>

double
jehanne_asin(double arg)
{
	double temp;
	int sign;

	sign = 0;
	if(arg < 0) {
		arg = -arg;
		sign++;
	}
	if(arg > 1)
		return jehanne_NaN();
	temp = jehanne_sqrt(1 - arg*arg);
	if(arg > 0.7)
		temp = PIO2 - jehanne_atan(temp/arg);
	else
		temp = jehanne_atan(arg/temp);
	if(sign)
		temp = -temp;
	return temp;
}

double
jehanne_acos(double arg)
{
	if(arg > 1 || arg < -1)
		return jehanne_NaN();
	return PIO2 - jehanne_asin(arg);
}
