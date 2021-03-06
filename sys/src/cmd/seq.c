/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>

double	min = 1.0;
double	max = 0.0;
double	incr = 1.0;
int	constant = 0;
int	nsteps;
char	*format;

void
usage(void)
{
	fprint(2, "usage: seq [-fformat] [-w] [first [incr]] last\n");
	exits("usage");
}

void
buildfmt(void)
{
	char *dp;
	int w, p, maxw, maxp;
	static char fmt[16];
	char buf[32];
	double val;

	format = "%g\n";
	if(!constant)
		return;
	maxw = 0;
	maxp = 0;
	for(val = min; val <= max; val += incr){
		sprint(buf, "%g", val);
		if(strchr(buf, 'e')!=0)
			return;
		dp = strchr(buf,'.');
		w = dp==0? strlen(buf): dp-buf;
		p = dp==0? 0: strlen(strchr(buf,'.')+1);
		if(w>maxw)
			maxw = w;
		if(p>maxp)
			maxp = p;
	}
	if(maxp > 0)
		maxw += maxp+1;
	sprint(fmt,"%%%d.%df\n", maxw, maxp);
	format = fmt;
}

void
main(int argc, char *argv[]){
	int j, n;
	char buf[256], ffmt[4096];
	double val;

	ARGBEGIN{
	case 'w':
		constant++;
		break;
	case 'f':
		format = EARGF(usage());
		if(format[strlen(format)-1] != '\n'){
			sprint(ffmt, "%s\n", format);
			format = ffmt;
		}
		break;
	default:
		goto out;
	}ARGEND
    out:
	if(argc<1 || argc>3)
		usage();
	max = atof(argv[argc-1]);
	if(argc > 1)
		min = atof(argv[0]);
	if(argc > 2)
		incr = atof(argv[1]);
	if(incr == 0){
		fprint(2, "seq: zero increment\n");
		exits("zero increment");
	}
	if(!format)
		buildfmt();
	if(incr > 0){
		for(val = min; val <= max; val += incr){
			n = sprint(buf, format, val);
			if(constant)
				for(j=0; buf[j]==' '; j++)
					buf[j] ='0';
			jehanne_write(1, buf, n);
		}
	}else{
		for(val = min; val >= max; val += incr){
			n = sprint(buf, format, val);
			if(constant)
				for(j=0; buf[j]==' '; j++)
					buf[j] ='0';
			jehanne_write(1, buf, n);
		}
	}
	exits(0);
}
