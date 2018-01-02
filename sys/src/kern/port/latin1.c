/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
#include "u.h"
#include "../port/lib.h"

/*
 * The code makes two assumptions: jehanne_strlen(ld) is 1 or 2; latintab[i].ld can be a
 * prefix of latintab[j].ld only when j<i.
 */
struct cvlist
{
	char	*ld;		/* must be seen before using this conversion */
	char	*si;		/* options for last input characters */
	Rune	*so;		/* the corresponding Rune for each si entry */
} latintab[] = {
#include "../port/latin1.h"
	0,	0,		0
};

/*
 * Given n characters 'X' k[1]..k[n-1], find the rune or return -1 for failure.
 */
long
unicode(Rune *k, int n)
{
	long i, c;

	k++;	/* skip 'X' */
	c = 0;
	for(i=0; i<n-1; i++,k++){
		c <<= 4;
		if('0'<=*k && *k<='9')
			c += *k-'0';
		else if('a'<=*k && *k<='f')
			c += 10 + *k-'a';
		else if('A'<=*k && *k<='F')
			c += 10 + *k-'A';
		else
			return -1;
	}
	if(c <= Runemax)
		return c;
	return -1;
}

/*
 * Given n characters k[0]..k[n-1], find the corresponding rune or return -1 for
 * failure, or something < -1 if n is too small.  In the latter case, the result
 * is minus the required n.
 */
static	char	esctab[] = {'X', 5, 'Y', 7};

long
latin1(Rune *k, int n)
{
	struct cvlist *l;
	int c, i;
	char* p;

	for(i = 0; i < nelem(esctab); i += 2)
		if(k[0] == esctab[i]){
			if(n>=esctab[i+1])
				return unicode(k, esctab[i+1]);
			else
				return -esctab[i+1];
		}
	for(l=latintab; l->ld!=0; l++)
		if(k[0] == l->ld[0]){
			if(n == 1)
				return -2;
			if(l->ld[1] == 0)
				c = k[1];
			else if(l->ld[1] != k[1])
				continue;
			else if(n == 2)
				return -3;
			else
				c = k[2];
			for(p=l->si; *p!=0; p++)
				if(*p == c)
					return l->so[p - l->si];
			return -1;
		}
	return -1;
}
