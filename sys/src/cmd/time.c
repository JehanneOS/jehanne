/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

char	output[4096];
void	add(char*, ...);
void	error(char*);
void	notifyf(void *c, char*);

void
main(int argc, char *argv[])
{
	int i;
	Waitmsg *w;
	int32_t l;
	char *p;
	char err[ERRMAX];

	if(argc <= 1){
		jehanne_fprint(2, "usage: time command\n");
		jehanne_exits("usage");
	}

	switch(jehanne_fork()){
	case -1:
		error("fork");
	case 0:
		exec(argv[1], &argv[1]);
		if(argv[1][0] != '/' && jehanne_strncmp(argv[1], "./", 2) &&
		   jehanne_strncmp(argv[1], "../", 3)){
			jehanne_sprint(output, "/cmd/%s", argv[1]);
			exec(output, &argv[1]);
		}
		error(argv[1]);
	}

	notify(notifyf);

    loop:
	w = jehanne_wait();
	if(w == nil){
		errstr(err, sizeof err);
		if(jehanne_strcmp(err, "interrupted") == 0)
			goto loop;
		error("wait");
	}
	l = w->time[0];
	add("%ld.%.2ldu", l/1000, (l%1000)/10);
	l = w->time[1];
	add("%ld.%.2lds", l/1000, (l%1000)/10);
	l = w->time[2];
	add("%ld.%.2ldr", l/1000, (l%1000)/10);
	add("\t");
	for(i=1; i<argc; i++){
		add("%s", argv[i], 0);
		if(i>4){
			add("...");
			break;
		}
	}
	if(w->msg[0]){
		p = jehanne_utfrune(w->msg, ':');
		if(p && p[1])
			p++;
		else
			p = w->msg;
		add(" # status=%s", p);
	}
	jehanne_fprint(2, "%s\n", output);
	jehanne_exits(w->msg);
}

void
add(char *a, ...)
{
	static int beenhere=0;
	va_list arg;

	if(beenhere)
		jehanne_strcat(output, " ");
	va_start(arg, a);
	jehanne_vseprint(output+jehanne_strlen(output), output+sizeof(output), a, arg);
	va_end(arg);
	beenhere++;
}

void
error(char *s)
{

	jehanne_fprint(2, "time: %s: %r\n", s);
	jehanne_exits(s);
}

void
notifyf(void *a, char *s)
{
	USED(a);
	if(jehanne_strcmp(s, "interrupt") == 0)
		noted(NCONT);
	noted(NDFLT);
}
