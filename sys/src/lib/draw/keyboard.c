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
#include <draw.h>
#include <thread.h>
#include <keyboard.h>


void
closekeyboard(Keyboardctl *kc)
{
	if(kc == nil)
		return;

	postnote(PNPROC, kc->pid, "kill");

#ifdef BUG
	/* Drain the channel */
	while(?kc->c)
		<-kc->c;
#endif

	sys_close(kc->ctlfd);
	sys_close(kc->consfd);
	free(kc->file);
	free(kc->c);
	free(kc);
}

static
void
_ioproc(void *arg)
{
	int m, n;
	char buf[20];
	Rune r;
	Keyboardctl *kc;

	kc = arg;
	threadsetname("kbdproc");
	kc->pid = getpid();
	n = 0;
	for(;;){
		while(n>0 && fullrune(buf, n)){
			m = chartorune(&r, buf);
			n -= m;
			memmove(buf, buf+m, n);
			send(kc->c, &r);
		}
		m = jehanne_read(kc->consfd, buf+n, sizeof buf-n);
		if(m <= 0){
			yield();	/* if error is due to exiting, we'll exit here */
			fprint(2, "keyboard read error: %r\n");
			threadexits("error");
		}
		n += m;
	}
}

Keyboardctl*
initkeyboard(char *file)
{
	Keyboardctl *kc;
	char *t;

	kc = mallocz(sizeof(Keyboardctl), 1);
	if(kc == nil)
		return nil;
	if(file == nil)
		file = "/dev/cons";
	kc->file = strdup(file);
	kc->consfd = sys_open(file, ORDWR|OCEXEC);
	t = malloc(strlen(file)+16);
	if(kc->consfd<0 || t==nil){
Error1:
		free(kc);
		return nil;
	}
	sprint(t, "%sctl", file);
	kc->ctlfd = sys_open(t, OWRITE|OCEXEC);
	if(kc->ctlfd < 0){
		fprint(2, "initkeyboard: can't open %s: %r\n", t);
Error2:
		sys_close(kc->consfd);
		free(t);
		goto Error1;
	}
	if(ctlkeyboard(kc, "rawon") < 0){
		fprint(2, "initkeyboard: can't turn on raw mode on %s: %r\n", t);
		sys_close(kc->ctlfd);
		goto Error2;
	}
	free(t);
	kc->c = chancreate(sizeof(Rune), 20);
	proccreate(_ioproc, kc, 4096);
	return kc;
}

int
ctlkeyboard(Keyboardctl *kc, char *m)
{
	return jehanne_write(kc->ctlfd, m, strlen(m));
}
