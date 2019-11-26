/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

void
getpasswd(char *p, int len)
{
	char c;
	int i, n, fd;

	fd = sys_open("#c/consctl", OWRITE);
	if(fd < 0)
		fatal("can't open consctl; please reboot");
	jehanne_write(fd, "rawon", 5);
 Prompt:
	jehanne_print("password: ");
	n = 0;
	for(;;){
		do{
			i = jehanne_read(0, &c, 1);
			if(i < 0)
				fatal("can't read cons; please reboot");
		}while(i == 0);
		switch(c){
		case '\n':
			p[n] = '\0';
			sys_close(fd);
			jehanne_print("\n");
			return;
		case '\b':
			if(n > 0)
				n--;
			break;
		case 'u' - 'a' + 1:		/* cntrl-u */
			jehanne_print("\n");
			goto Prompt;
		default:
			if(n < len - 1)
				p[n++] = c;
			break;
		}
	}
}
