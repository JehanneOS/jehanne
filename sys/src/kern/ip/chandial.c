/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"../ip/ip.h"

typedef struct DS DS;
static Chan*	call(char*, char*, DS*);
static void	_dial_string_parse(char*, DS*);

enum
{
	Maxstring=	128,
};

struct DS
{
	char	buf[Maxstring];			/* dist string */
	char	*netdir;
	char	*proto;
	char	*rem;
	char	*local;				/* other args */
	char	*dir;
	Chan	**ctlp;
};

/*
 *  the dialstring is of the form '[/net/]proto!dest'
 */
Chan*
chandial(char *dest, char *local, char *dir, Chan **ctlp)
{
	DS ds;
	char clone[Maxpath];

	ds.local = local;
	ds.dir = dir;
	ds.ctlp = ctlp;

	_dial_string_parse(dest, &ds);
	if(ds.netdir == 0)
		ds.netdir = "/net";

	/* no connection server, don't translate */
	jehanne_snprint(clone, sizeof(clone), "%s/%s/clone", ds.netdir, ds.proto);
	return call(clone, ds.rem, &ds);
}

static Chan*
call(char *clone, char *dest, DS *ds)
{
	int n;
	Chan *dchan, *cchan;
	char name[Maxpath], data[Maxpath], *p;

	cchan = namec(clone, Aopen, ORDWR, 0);

	/* get directory name */
	if(waserror()){
		cclose(cchan);
		nexterror();
	}
	n = cchan->dev->read(cchan, name, sizeof(name)-1, 0);
	name[n] = 0;
	for(p = name; *p == ' '; p++)
		;
	jehanne_sprint(name, "%lud", jehanne_strtoul(p, 0, 0));
	p = jehanne_strrchr(clone, '/');
	*p = 0;
	if(ds->dir)
		jehanne_snprint(ds->dir, Maxpath, "%s/%s", clone, name);
	jehanne_snprint(data, sizeof(data), "%s/%s/data", clone, name);

	/* connect */
	if(ds->local)
		jehanne_snprint(name, sizeof(name), "connect %s %s", dest, ds->local);
	else
		jehanne_snprint(name, sizeof(name), "connect %s", dest);
	cchan->dev->write(cchan, name, jehanne_strlen(name), 0);

	/* open data connection */
	dchan = namec(data, Aopen, ORDWR, 0);
	if(ds->ctlp)
		*ds->ctlp = cchan;
	else
		cclose(cchan);
	poperror();
	return dchan;

}

/*
 *  parse a dial string
 */
static void
_dial_string_parse(char *str, DS *ds)
{
	char *p, *p2;

	jehanne_strncpy(ds->buf, str, Maxstring);
	ds->buf[Maxstring-1] = 0;

	p = jehanne_strchr(ds->buf, '!');
	if(p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->rem = ds->buf;
	} else {
		if(*ds->buf != '/' && *ds->buf != '#'){
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for(p2 = p; *p2 != '/'; p2--)
				;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->rem = p + 1;
	}
}
