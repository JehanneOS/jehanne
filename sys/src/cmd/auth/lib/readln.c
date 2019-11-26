/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include <u.h>
#include <lib9.h>
#include <authsrv.h>
#include <bio.h>
#include "authcmdlib.h"

void
getpass(Authkey *key, char *pass, int check, int confirm)
{
	char rpass[32], npass[32];
	char *err;

	if(pass == nil)
		pass = npass;

	for(;;){
		readln("Password: ", pass, sizeof npass, 1);
		if(confirm){
			readln("Confirm password: ", rpass, sizeof rpass, 1);
			if(strcmp(pass, rpass) != 0){
				print("mismatch, try again\n");
				continue;
			}
		}
		if(check)
			if(err = okpasswd(pass)){
				print("%s, try again\n", err);
				continue;
			}
		passtokey(key, pass);
		break;
	}
}

int
getsecret(int passvalid, char *p9pass)
{
	char answer[32];

	readln("assign Inferno/POP secret? (y/n) ", answer, sizeof answer, 0);
	if(*answer != 'y' && *answer != 'Y')
		return 0;

	if(passvalid){
		readln("make it the same as the plan 9 password? (y/n) ",
			answer, sizeof answer, 0);
		if(*answer == 'y' || *answer == 'Y')
			return 1;
	}

	for(;;){
		readln("Secret(0 to 256 characters): ", p9pass,
			sizeof answer, 1);
		readln("Confirm: ", answer, sizeof answer, 1);
		if(strcmp(p9pass, answer) == 0)
			break;
		print("mismatch, try again\n");
	}
	return 1;
}

void
readln(char *prompt, char *line, int len, int raw)
{
	char *p;
	int fdin, fdout, ctl, n, nr;

	fdin = sys_open("/dev/cons", OREAD);
	fdout = sys_open("/dev/cons", OWRITE);
	fprint(fdout, "%s", prompt);
	if(raw){
		ctl = sys_open("/dev/consctl", OWRITE);
		if(ctl < 0)
			error("couldn't set raw mode");
		jehanne_write(ctl, "rawon", 5);
	} else
		ctl = -1;
	nr = 0;
	p = line;
	for(;;){
		n = jehanne_read(fdin, p, 1);
		if(n < 0){
			sys_close(ctl);
			error("can't read cons\n");
		}
		if(*p == 0x7f)
			exits(0);
		if(n == 0 || *p == '\n' || *p == '\r'){
			*p = '\0';
			if(raw){
				jehanne_write(ctl, "rawoff", 6);
				jehanne_write(fdout, "\n", 1);
			}
			sys_close(ctl);
			return;
		}
		if(*p == '\b'){
			if(nr > 0){
				nr--;
				p--;
			}
		}else{
			nr++;
			p++;
		}
		if(nr == len){
			fprint(fdout, "line too int32_t; try again\n");
			nr = 0;
			p = line;
		}
	}
}
