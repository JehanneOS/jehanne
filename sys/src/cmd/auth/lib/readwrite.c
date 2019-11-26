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

int
readfile(char *file, char *buf, int n)
{
	int fd;

	fd = sys_open(file, OREAD);
	if(fd < 0){
		werrstr("%s: %r", file);
		return -1;
	}
	n = jehanne_read(fd, buf, n);
	sys_close(fd);
	return n;
}

int
writefile(char *file, char *buf, int n)
{
	int fd;

	fd = sys_open(file, OWRITE);
	if(fd < 0)
		return -1;
	n = jehanne_write(fd, buf, n);
	sys_close(fd);
	return n;
}

char*
finddeskey(char *db, char *user, char *key)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/key", db, user);
	n = readfile(filename, key, DESKEYLEN);
	if(n != DESKEYLEN)
		return nil;
	else
		return key;
}

uint8_t*
findaeskey(char *db, char *user, uint8_t *key)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/aeskey", db, user);
	n = readfile(filename, (char*)key, AESKEYLEN);
	if(n != AESKEYLEN)
		return nil;
	else
		return key;
}

int
findkey(char *db, char *user, Authkey *key)
{
	int ret;

	memset(key, 0, sizeof(Authkey));
	ret = findaeskey(db, user, key->aes) != nil;
	if(ret){
		char filename[Maxpath];
		snprint(filename, sizeof filename, "%s/%s/pakhash", db, user);
		if(readfile(filename, (char*)key->pakhash, PAKHASHLEN) != PAKHASHLEN)
			authpak_hash(key, user);
	}
	ret |= finddeskey(db, user, key->des) != nil;
	return ret;
}

char*
findsecret(char *db, char *user, char *secret)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/secret", db, user);
	n = readfile(filename, secret, SECRETLEN-1);
	secret[n]=0;
	if(n <= 0)
		return nil;
	else
		return secret;
}

char*
setdeskey(char *db, char *user, char *key)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/key", db, user);
	n = writefile(filename, key, DESKEYLEN);
	if(n != DESKEYLEN)
		return nil;
	else
		return key;
}

uint8_t*
setaeskey(char *db, char *user, uint8_t *key)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/aeskey", db, user);
	n = writefile(filename, (char*)key, AESKEYLEN);
	if(n != AESKEYLEN)
		return nil;
	else
		return key;
}

int
setkey(char *db, char *user, Authkey *key)
{
	int ret;

	ret = setdeskey(db, user, key->des) != nil;
	ret |= setaeskey(db, user, key->aes) != nil;
	return ret;
}

char*
setsecret(char *db, char *user, char *secret)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/secret", db, user);
	n = writefile(filename, secret, strlen(secret));
	if(n != strlen(secret))
		return nil;
	else
		return secret;
}
