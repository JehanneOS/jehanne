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
#include <chartypes.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

/*
 * get the date in the format yyyymmdd
 */
Tm
getdate(char *d)
{
	Tm date;
	int i;

	memset(&date, 0, sizeof(date));
	for(i = 0; i < 8; i++)
		if(!isdigit(d[i]))
			return date;
	date.year = (d[0]-'0')*1000 + (d[1]-'0')*100 + (d[2]-'0')*10 + d[3]-'0';
	date.year -= 1900;
	d += 4;
	date.mon = (d[0]-'0')*10 + d[1]-'0' - 1;
	d += 2;
	date.mday = (d[0]-'0')*10 + d[1]-'0';
	return date;
}

int32_t
getexpiration(char *db, char *u)
{
	char buf[Maxpath];
	char prompt[128];
	char cdate[32];
	Tm date;
	uint32_t secs, now;
	int n, fd;

	/* read current expiration (if any) */
	snprint(buf, sizeof buf, "%s/%s/expire", db, u);
	fd = sys_open(buf, OREAD);
	buf[0] = 0;
	if(fd >= 0){
		n = jehanne_read(fd, buf, sizeof(buf)-1);
		if(n > 0)
			buf[n-1] = 0;
		sys_close(fd);
	}

	if(buf[0]){
		if(strncmp(buf, "never", 5)){
			secs = atoi(buf);
			memmove(&date, localtime(secs), sizeof(date));
			sprint(buf, "%4.4d%2.2d%2.2d", date.year+1900, date.mon+1, date.mday);
		} else
			buf[5] = 0;
	} else
		strcpy(buf, "never");
	sprint(prompt, "Expiration date (YYYYMMDD or never)[return = %s]: ", buf);

	now = time(0);
	for(;;){
		readln(prompt, cdate, sizeof cdate, 0);
		if(*cdate == 0)
			return -1;
		if(strcmp(cdate, "never") == 0)
			return 0;
		date = getdate(cdate);
		secs = tm2sec(&date);
		if(secs > now && secs < now + 2*365*24*60*60)
			break;
		print("expiration time must fall between now and 2 years from now\n");
	}
	return secs;
}
