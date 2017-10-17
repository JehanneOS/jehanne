/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "rc.h"
#include "exec.h"
#include "fns.h"

/*
 * delete all the GLOB marks from s, in place
 */

char*
deglob(char *s)
{
	char *b = s;
	char *t = s;
	do{
		if(*t==GLOB)
			t++;
		*s++=*t;
	}while(*t++);
	return b;
}

int
globcmp(const void *s, const void *t)
{
	return strcmp(*(char**)s, *(char**)t);
}

void
globsort(word *left, word *right)
{
	char **list;
	word *a;
	int n = 0;
	for(a = left;a!=right;a = a->next) n++;
	list = (char **)emalloc(n*sizeof(char *));
	for(a = left,n = 0;a!=right;a = a->next,n++) list[n] = a->word;
	qsort((void *)list, n, sizeof(void *), globcmp);
	for(a = left,n = 0;a!=right;a = a->next,n++) a->word = list[n];
	free(list);
}

/*
 * Does the string s match the pattern p
 * . and .. are only matched by patterns starting with .
 * * matches any sequence of characters
 * ? matches any single character
 * [...] matches the enclosed list of characters
 */

static int
matchfn(char *s, char *p)
{
	if(s[0]=='.' && (s[1]=='\0' || s[1]=='.' && s[2]=='\0') && p[0]!='.')
		return 0;
	return match(s, p, '/');
}

static word*
globdir(word *list, char *p, char *name, char *namep)
{
	char *t, *newp;
	int f;
	/* scan the pattern looking for a component with a metacharacter in it */
	if(*p=='\0')
		return newword(name, list);
	t = namep;
	newp = p;
	while(*newp){
		if(*newp==GLOB)
			break;
		*t=*newp++;
		if(*t++=='/'){
			namep = t;
			p = newp;
		}
	}
	/* If we ran out of pattern, append the name if accessible */
	if(*newp=='\0'){
		*t='\0';
		if(access(name, AEXIST)==0)
			list = newword(name, list);
		return list;
	}
	/* read the directory and recur for any entry that matches */
	*namep='\0';
	if((f = Opendir(name[0]?name:".")) >= 0){
		while(*newp!='/' && *newp!='\0') newp++;
		while(Readdir(f, namep, *newp=='/')){
			if(matchfn(namep, p)){
				for(t = namep;*t;t++);
				list = globdir(list, newp, name, t);
			}
		}
		Closedir(f);
	}
	return list;
}

/*
 * Subsitute a word with its glob in place.
 */

static void
globword(word *w)
{
	word *left, *right;
	char *name;

	if(w->glob == 0)
		return;
	name = emalloc(w->glob);
	memset(name, 0, w->glob);
	right = w->next;
	left = globdir(right, w->word, name, name);
	free(name);
	if(left == right) {
		deglob(w->word);
		w->glob = 0;
	} else {
		free(w->word);
		globsort(left, right);
		*w = *left;
		free(left);
	}
}

word*
globlist(word *l)
{
	word *p, *q;

	for(p=l;p;p=q){
		q = p->next;
		globword(p);
	}
	return l;
}

/*
 * Return a pointer to the next utf code in the string,
 * not jumping past nuls in broken utf codes!
 */

static char*
nextutf(char *p)
{
	Rune dummy;

	return p + chartorune(&dummy, p);
}

/*
 * Convert the utf code at *p to a unicode value
 */

static int
unicode(char *p)
{
	Rune r;

	chartorune(&r, p);
	return r;
}

/*
 * Do p and q point at equal utf codes
 */

static int
equtf(char *p, char *q)
{
	if(*p!=*q)
 		return 0;
	return unicode(p) == unicode(q);
}

int
match(char *s, char *p, int stop)
{
	int compl, hit, lo, hi, t, c;

	for(; *p!=stop && *p!='\0'; s = nextutf(s), p = nextutf(p)){
		if(*p!=GLOB){
			if(!equtf(p, s)) return 0;
		}
		else switch(*++p){
		case GLOB:
			if(*s!=GLOB)
				return 0;
			break;
		case '*':
			for(;;){
				if(match(s, nextutf(p), stop)) return 1;
				if(!*s)
					break;
				s = nextutf(s);
			}
			return 0;
		case '?':
			if(*s=='\0')
				return 0;
			break;
		case '[':
			if(*s=='\0')
				return 0;
			c = unicode(s);
			p++;
			compl=*p=='~';
			if(compl)
				p++;
			hit = 0;
			while(*p!=']'){
				if(*p=='\0')
					return 0;		/* syntax error */
				lo = unicode(p);
				p = nextutf(p);
				if(*p!='-')
					hi = lo;
				else{
					p++;
					if(*p=='\0')
						return 0;	/* syntax error */
					hi = unicode(p);
					p = nextutf(p);
					if(hi<lo){ t = lo; lo = hi; hi = t; }
				}
				if(lo<=c && c<=hi)
					hit = 1;
			}
			if(compl)
				hit=!hit;
			if(!hit)
				return 0;
			break;
		}
	}
	return *s=='\0';
}
