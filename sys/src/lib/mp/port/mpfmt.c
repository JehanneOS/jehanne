#include "os.h"
#include <mp.h>
#include "dat.h"

static int
toencx(mpint *b, char *buf, int len, int (*enc)(char*, int, const uint8_t*, int))
{
	uint8_t *p;
	int n, rv;

	p = nil;
	n = mptobe(b, nil, 0, &p);
	if(n < 0)
		return -1;
	rv = (*enc)(buf, len, p, n);
	jehanne_free(p);
	return rv;
}

static char set16[] = "0123456789ABCDEF";

static int
topow2(mpint *b, char *buf, int len, int s)
{
	mpdigit *p, x;
	int i, j, sn;
	char *out, *eout;

	if(len < 1)
		return -1;

	sn = 1<<s;
	out = buf;
	eout = buf+len;
	for(p = &b->p[b->top-1]; p >= b->p; p--){
		x = *p;
		for(i = Dbits-s; i >= 0; i -= s){
			j = x >> i & sn - 1;
			if(j != 0 || out != buf){
				if(out >= eout)
					return -1;
				*out++ = set16[j];
			}
		}
	}
	if(out == buf)
		*out++ = '0';
	if(out >= eout)
		return -1;
	*out = 0;
	return 0;
}

static char*
modbillion(int rem, uint32_t r, char *out, char *buf)
{
	uint32_t rr;
	int i;

	for(i = 0; i < 9; i++){
		rr = r%10;
		r /= 10;
		if(out <= buf)
			return nil;
		*--out = '0' + rr;
		if(rem == 0 && r == 0)
			break;
	}
	return out;
}

static int
to10(mpint *b, char *buf, int len)
{
	mpint *d, *r, *billion;
	char *out;

	if(len < 1)
		return -1;

	d = mpcopy(b);
	d->flags &= ~MPtimesafe;
	mpnorm(d);
	r = mpnew(0);
	billion = uitomp(1000000000, nil);
	out = buf+len;
	*--out = 0;
	do {
		mpdiv(d, billion, d, r);
		out = modbillion(d->top, r->p[0], out, buf);
		if(out == nil)
			break;
	} while(d->top != 0);
	mpfree(d);
	mpfree(r);
	mpfree(billion);

	if(out == nil)
		return -1;
	len -= out-buf;
	if(out != buf)
		jehanne_memmove(buf, out, len);
	return 0;
}

static int
to8(mpint *b, char *buf, int len)
{
	mpdigit x, y;
	char *out;
	int i, j;

	if(len < 2)
		return -1;

	out = buf+len;
	*--out = 0;

	i = j = 0;
	x = y = 0;
	while(j < b->top){
		y = b->p[j++];
		if(i > 0)
			x |= y << i;
		else
			x = y;
		i += Dbits;
		while(i >= 3){
Digout:			i -= 3;
			if(out > buf)
				out--;
			else if(x != 0)
				return -1;
			*out = '0' + (x & 7);
			x = y >> Dbits-i;
		}
	}
	if(i > 0)
		goto Digout;

	while(*out == '0') out++;
	if(*out == '\0')
		*--out = '0';

	len -= out-buf;
	if(out != buf)
		jehanne_memmove(buf, out, len);
	return 0;
}

int
mpfmt(Fmt *fmt)
{
	mpint *b;
	char *x, *p;
	int base;

	b = va_arg(fmt->args, mpint*);
	if(b == nil)
		return jehanne_fmtstrcpy(fmt, "*");

	base = fmt->prec;
	if(base == 0)
		base = 16;	/* default */
	fmt->flags &= ~FmtPrec;
	p = mptoa(b, base, nil, 0);
	if(p == nil)
		return jehanne_fmtstrcpy(fmt, "*");
	else{
		if((fmt->flags & FmtSharp) != 0){
			switch(base){
			case 16:
				x = "0x";
				break;
			case 8:
				x = "0";
				break;
			case 2:
				x = "0b";
				break;
			default:
				x = "";
			}
			if(*p == '-')
				jehanne_fmtprint(fmt, "-%s%s", x, p + 1);
			else
				jehanne_fmtprint(fmt, "%s%s", x, p);
		}
		else
			jehanne_fmtstrcpy(fmt, p);
		jehanne_free(p);
		return 0;
	}
}

char*
mptoa(mpint *b, int base, char *buf, int len)
{
	char *out;
	int rv, alloced;

	if(base == 0)
		base = 16;	/* default */
	alloced = 0;
	if(buf == nil){
		/* rv <= log₂(base) */
		for(rv=1; (base >> rv) > 1; rv++)
			;
		len = 10 + (b->top*Dbits / rv);
		buf = jehanne_malloc(len);
		if(buf == nil)
			return nil;
		alloced = 1;
	}

	if(len < 2)
		return nil;

	out = buf;
	if(b->sign < 0){
		*out++ = '-';
		len--;
	}
	switch(base){
	case 64:
		rv = toencx(b, out, len, jehanne_enc64);
		break;
	case 32:
		rv = toencx(b, out, len, jehanne_enc32);
		break;
	case 16:
		rv = topow2(b, out, len, 4);
		break;
	case 10:
		rv = to10(b, out, len);
		break;
	case 8:
		rv = to8(b, out, len);
		break;
	case 4:
		rv = topow2(b, out, len, 2);
		break;
	case 2:
		rv = topow2(b, out, len, 1);
		break;
	default:
		abort();
		return nil;
	}
	if(rv < 0){
		if(alloced)
			jehanne_free(buf);
		return nil;
	}
	return buf;
}
