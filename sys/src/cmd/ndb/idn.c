#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dns.h"

enum {
	base = 36,
	tmin = 1,
	tmax = 26,
	skew = 38,
	damp = 700,
	initial_bias = 72,
	initial_n = 0x80,
};

static uint maxint = ~0;

static uint
decode_digit(uint cp)
{
	if((cp - '0') < 10)
		return cp - ('0' - 26);
	if((cp - 'A') < 26)
		return cp - 'A';
	if((cp - 'a') < 26)
		return cp - 'a';
	return base;
}

static char
encode_digit(uint d, int flag)
{
	if(d < 26)
		return d + (flag ? 'A' : 'a');
	return d + ('0' - 26);
}

static uint
adapt(uint delta, uint numpoints, int firsttime)
{
	uint k;

	delta = firsttime ? delta / damp : delta >> 1;
	delta += delta / numpoints;
	for (k = 0; delta > ((base - tmin) * tmax) / 2; k += base)
		delta /= base - tmin;
	return k + (base - tmin + 1) * delta / (delta + skew);
}

static int
punyencode(uint input_length, Rune input[], uint max_out, char output[])
{
	uint n, delta, h, b, out, bias, j, m, q, k, t;

	n = initial_n;
	delta = out = 0;
	bias = initial_bias;

	for (j = 0;  j < input_length;  ++j) {
		if ((uint)input[j] < 0x80) {
			if (max_out - out < 2)
				return -1;
			output[out++] = input[j];
		}
	}

	h = b = out;

	if (b > 0)
		output[out++] = '-';

	while (h < input_length) {
		for (m = maxint, j = 0; j < input_length; ++j) {
			if (input[j] >= n && input[j] < m)
				m = input[j];
		}

		if (m - n > (maxint - delta) / (h + 1))
			return -1;

		delta += (m - n) * (h + 1);
		n = m;

		for (j = 0;  j < input_length;  ++j) {
			if (input[j] < n) {
				if (++delta == 0)
					return -1;
			}

			if (input[j] == n) {
				for (q = delta, k = base;; k += base) {
					if (out >= max_out)
						return -1;
					if (k <= bias)
						t = tmin;
					else if (k >= bias + tmax)
						t = tmax;
					else
						t = k - bias;
					if (q < t)
						break;
					output[out++] = encode_digit(t + (q - t) % (base - t), 0);
					q = (q - t) / (base - t);
				}
				output[out++] = encode_digit(q, isupperrune(input[j]));
				bias = adapt(delta, h + 1, h == b);
				delta = 0;
				++h;
			}
		}

		++delta, ++n;
	}

	return (int)out;
}

static int
punydecode(uint input_length, char input[], uint max_out, Rune output[])
{
	uint n, out, i, bias, b, j, in, oldi, w, k, digit, t;

	n = initial_n;
	out = i = 0;
	bias = initial_bias;

	for (b = j = 0; j < input_length; ++j)
		if (input[j] == '-')
			b = j;

	if (b > max_out)
		return -1;

	for (j = 0;  j < b;  ++j) {
		if (input[j] & 0x80)
			return -1;
		output[out++] = input[j];
	}

	for (in = b > 0 ? b + 1 : 0; in < input_length; ++out) {
		for (oldi = i, w = 1, k = base;; k += base) {
			if (in >= input_length)
				return -1;
			digit = decode_digit(input[in++]);
			if (digit >= base)
				return -1;
			if (digit > (maxint - i) / w)
				return -1;
			i += digit * w;
			if (k <= bias)
				t = tmin;
			else if (k >= bias + tmax)
				t = tmax;
			else
				t = k - bias;
			if (digit < t)
				break;
			if (w > maxint / (base - t))
				return -1;
			w *= (base - t);
		}

		bias = adapt(i - oldi, out + 1, oldi == 0);

		if (i / (out + 1) > maxint - n)
			return -1;
		n += i / (out + 1);
		i %= (out + 1);

		if (out >= max_out)
			return -1;

		memmove(output + i + 1, output + i, (out - i) * sizeof *output);
		if(((uint)input[in-1] - 'A') < 26)
			output[i++] = toupperrune(n);
		else
			output[i++] = tolowerrune(n);
	}

	return (int)out;
}

/*
 * convert punycode encoded internationalized
 * domain name to unicode string
 */
char*
idn2utf(char *name, char *buf, int nbuf)
{
	char *dp, *de, *cp;
	Rune rb[Domlen], r;
	int nc, nr, n;

	cp = name;
	dp = buf;
	de = dp+nbuf-1;
	for(;;){
		nc = nr = 0;
		while(cp[nc] != 0){
			n = chartorune(&r, cp+nc);
			if(r == '.')
				break;
			rb[nr++] = r;
			nc += n;
		}
		if(cistrncmp(cp, "xn--", 4) == 0)
			if((nr = punydecode(nc-4, cp+4, nelem(rb), rb)) < 0)
				return nil;
		dp = seprint(dp, de, "%.*S", nr, rb);
		if(dp >= de)
			return nil;
		if(cp[nc] == 0)
			break;
		*dp++ = '.';
		cp += nc+1;
	}
	*dp = 0;
	return buf;
}

/*
 * convert unicode string to punycode
 * encoded internationalized domain name
 */
char*
utf2idn(char *name, char *buf, int nbuf)
{
	char *dp, *de, *cp;
	Rune rb[Domlen], r;
	int nc, nr, n;

	dp = buf;
	de = dp+nbuf-1;
	cp = name;
	for(;;){
		nc = nr = 0;
		while(cp[nc] != 0 && nr < nelem(rb)){
			n = chartorune(&r, cp+nc);
			if(r == '.')
				break;
			rb[nr++] = r;
			nc += n;
		}
		if(nc == nr)
			dp = seprint(dp, de, "%.*s", nc, cp);
		else {
			dp = seprint(dp, de, "xn--");
			if((n = punyencode(nr, rb, de - dp, dp)) < 0)
				return nil;
			dp += n;
		}
		if(dp >= de)
			return nil;
		if(cp[nc] == 0)
			break;
		*dp++ = '.';
		cp += nc+1;
	}
	*dp = 0;
	return buf;
}

