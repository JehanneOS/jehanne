#include "os.h"
#include <mp.h>
#include "dat.h"

// division ala knuth, seminumerical algorithms, pp 237-238
// the numbers are stored backwards to what knuth expects so j
// counts down rather than up.

void
mpdiv(mpint *dividend, mpint *divisor, mpint *quotient, mpint *remainder)
{
	int j, s, vn, sign;
	mpdigit qd, *up, *vp, *qp;
	mpint *u, *v, *t;

	assert(quotient != remainder);
	assert(divisor->flags & MPnorm);

	// divide bv zero
	if(divisor->top == 0)
		abort();

	// division by one or small powers of two
	if(divisor->top == 1 && (divisor->p[0] & divisor->p[0]-1) == 0){
		int64_t r = (int64_t)dividend->sign * (dividend->p[0] & divisor->p[0]-1);
		if(quotient != nil){
			for(s = 0; ((divisor->p[0] >> s) & 1) == 0; s++)
				;
			mpright(dividend, s, quotient);
		}
		if(remainder != nil){
			remainder->flags |= dividend->flags & MPtimesafe;
			vtomp(r, remainder);
		}
		return;
	}
	assert((dividend->flags & MPtimesafe) == 0);

	// quick check
	if(mpmagcmp(dividend, divisor) < 0){
		if(remainder != nil)
			mpassign(dividend, remainder);
		if(quotient != nil)
			mpassign(mpzero, quotient);
		return;
	}

	// D1: shift until divisor, v, has hi bit set (needed to make trial
	//     divisor accurate)
	qd = divisor->p[divisor->top-1];
	for(s = 0; (qd & mpdighi) == 0; s++)
		qd <<= 1;
	u = mpnew((dividend->top+2)*Dbits + s);
	if(s == 0 && divisor != quotient && divisor != remainder) {
		mpassign(dividend, u);
		v = divisor;
	} else {
		mpleft(dividend, s, u);
		v = mpnew(divisor->top*Dbits);
		mpleft(divisor, s, v);
	}
	up = u->p+u->top-1;
	vp = v->p+v->top-1;
	vn = v->top;

	// D1a: make sure high digit of dividend is less than high digit of divisor
	if(*up >= *vp){
		*++up = 0;
		u->top++;
	}

	// storage for multiplies
	t = mpnew(4*Dbits);

	qp = nil;
	if(quotient != nil){
		mpbits(quotient, (u->top - v->top)*Dbits);
		quotient->top = u->top - v->top;
		qp = quotient->p+quotient->top-1;
	}

	// D2, D7: loop on length of dividend
	for(j = u->top; j > vn; j--){

		// D3: calculate trial divisor
		mpdigdiv(up-1, *vp, &qd);

		// D3a: rule out trial divisors 2 greater than real divisor
		if(vn > 1) for(;;){
			jehanne_memset(t->p, 0, 3*Dbytes);	// mpvecdigmuladd adds to what's there
			mpvecdigmuladd(vp-1, 2, qd, t->p);
			if(mpveccmp(t->p, 3, up-2, 3) > 0)
				qd--;
			else
				break;
		}

		// D4: u -= v*qd << j*Dbits
		sign = mpvecdigmulsub(v->p, vn, qd, up-vn);
		if(sign < 0){

			// D6: trial divisor was too high, add back borrowed
			//     value and decrease divisor
			mpvecadd(up-vn, vn+1, v->p, vn, up-vn);
			qd--;
		}

		// D5: save quotient digit
		if(qp != nil)
			*qp-- = qd;

		// push top of u down one
		u->top--;
		*up-- = 0;
	}
	if(qp != nil){
		assert((quotient->flags & MPtimesafe) == 0);
		mpnorm(quotient);
		if(dividend->sign != divisor->sign)
			quotient->sign = -1;
	}

	if(remainder != nil){
		assert((remainder->flags & MPtimesafe) == 0);
		mpright(u, s, remainder);	// u is the remainder shifted
		remainder->sign = dividend->sign;
	}

	mpfree(t);
	mpfree(u);
	if(v != divisor)
		mpfree(v);
}
