/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

uint8_t statbuf[Statsz];

int
cache(int fd)
{
	int argc, i, p[2];
	char *argv[5], bd[32], buf[256], partition[64], *pp;

	if(jehanne_stat("/boot/cfs", statbuf, sizeof statbuf) < 0)
		return fd;

	*partition = 0;

	sys_bind("#S", "/dev", MAFTER);
	readfile("#e/cfs", buf, sizeof(buf));
	if(*buf){
		argc = jehanne_tokenize(buf, argv, 4);
		for(i = 0; i < argc; i++){
			if(jehanne_strcmp(argv[i], "off") == 0)
				return fd;
			else if(jehanne_stat(argv[i], statbuf, sizeof statbuf) >= 0){
				jehanne_strncpy(partition, argv[i], sizeof(partition)-1);
				partition[sizeof(partition)-1] = 0;
			}
		}
	}

	if(*partition == 0){
		readfile("#e/bootdisk", bd, sizeof(bd));
		if(*bd){
			if(pp = jehanne_strchr(bd, ':'))
				*pp = 0;
			/* damned artificial intelligence */
			i = jehanne_strlen(bd);
			if(jehanne_strcmp("disk", &bd[i-4]) == 0)
				bd[i-4] = 0;
			else if(jehanne_strcmp("fs", &bd[i-2]) == 0)
				bd[i-2] = 0;
			else if(jehanne_strcmp("fossil", &bd[i-6]) == 0)
				bd[i-6] = 0;
			jehanne_sprint(partition, "%scache", bd);
			if(jehanne_stat(partition, statbuf, sizeof statbuf) < 0)
				*bd = 0;
		}
		if(*bd == 0){
			jehanne_sprint(partition, "%scache", bootdisk);
			if(jehanne_stat(partition, statbuf, sizeof statbuf) < 0)
				return fd;
		}
	}

	jehanne_print("cfs...");
	if(jehanne_pipe(p)<0)
		fatal("pipe");
	switch(jehanne_fork()){
	case -1:
		fatal("fork");
	case 0:
		sys_close(p[1]);
		if(jehanne_dup(fd, 0) != 0)
			fatal("jehanne_dup(fd, 0)");
		sys_close(fd);
		if(jehanne_dup(p[0], 1) != 1)
			fatal("jehanne_dup(p[0], 1)");
		sys_close(p[0]);
		if(fflag)
			jehanne_execl("/boot/cfs", "bootcfs", "-rs", "-f", partition, 0);
		else
			jehanne_execl("/boot/cfs", "bootcfs", "-s", "-f", partition, 0);
		break;
	default:
		sys_close(p[0]);
		sys_close(fd);
		fd = p[1];
		break;
	}
	return fd;
}
