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

/*
 * watchdog framework
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

enum {
	Qdir,
	Qwdctl,
};

/*
 * these are exposed so that delay() and the like can disable the watchdog
 * before busy looping for a long time.
 */
Watchdog*watchdog;
int	watchdogon;

static Watchdog *wd;
static int wdautopet;
static int wdclock0called;
static Ref refs;
static Dirtab wddir[] = {
	".",		{ Qdir, 0, QTDIR },	0,		0555,
	"wdctl",	{ Qwdctl, 0 },		0,		0664,
};


void
addwatchdog(Watchdog *wdog)
{
	if(wd){
		jehanne_print("addwatchdog: watchdog already installed\n");
		return;
	}
	wd = watchdog = wdog;
	if(wd)
		wd->disable();
}

static int
wdallowed(void)
{
	return getconf("*nowatchdog") == nil;
}

static void
wdshutdown(void)
{
	if (wd) {
		wd->disable();
		watchdogon = 0;
	}
}

/* called from clock interrupt, so restart needs ilock internally */
static void
wdpet(void)
{
	/* watchdog could be paused; if so, don't restart */
	if (wdautopet && watchdogon)
		wd->restart();
}

/*
 * reassure the watchdog from the clock interrupt
 * until the user takes control of it.
 */
static void
wdautostart(void)
{
	if (wdautopet || !wd || !wdallowed())
		return;
	if (waserror()) {
		jehanne_print("watchdog: automatic enable failed\n");
		return;
	}
	wd->enable();
	poperror();

	wdautopet = watchdogon = 1;
	if (!wdclock0called) {
		addclock0link(wdpet, 200);
		wdclock0called = 1;
	}
}

/*
 * disable strokes from the clock interrupt.
 * have to disable the watchdog to mark it `not in use'.
 */
static void
wdautostop(void)
{
	if (!wdautopet)
		return;
	wdautopet = 0;
	wdshutdown();
}

/*
 * user processes exist and up is non-nil when the
 * device init routines are called.
 */
static void
wdinit(void)
{
	wdautostart();
}

static Chan*
wdattach(Chan *c, Chan *ac, char *spec, int flags)
{
	return devattach('w', spec);
}

static Walkqid*
wdwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, wddir, nelem(wddir), devgen);
}

static long
wdstat(Chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, wddir, nelem(wddir), devgen);
}

static Chan*
wdopen(Chan* c, int omode)
{
	wdautostop();
	c = devopen(c, omode, wddir, nelem(wddir), devgen);
	if (c->qid.path == Qwdctl)
		incref(&refs);
	return c;
}

static void
wdclose(Chan *c)
{
	if(c->qid.path == Qwdctl && c->flag&COPEN && decref(&refs) <= 0)
		wdshutdown();
}

static long
wdread(Chan* c, void* a, long n, int64_t off)
{
	uint32_t offset = off;
	char *p;

	switch((uint32_t)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, wddir, nelem(wddir), devgen);

	case Qwdctl:
		if(wd == nil || wd->stat == nil)
			return 0;

		p = jehanne_malloc(READSTR);
		if(p == nil)
			error(Enomem);
		if(waserror()){
			jehanne_free(p);
			nexterror();
		}

		wd->stat(p, p + READSTR);
		n = readstr(offset, a, n, p);
		jehanne_free(p);
		poperror();
		return n;

	default:
		error(Egreg);
		break;
	}
	return 0;
}

static long
wdwrite(Chan* c, void* a, long n, int64_t off)
{
	uint32_t offset = off;
	char *p;

	switch((uint32_t)c->qid.path){
	case Qdir:
		error(Eperm);

	case Qwdctl:
		if(wd == nil)
			return n;

		if(offset || n >= READSTR)
			error(Ebadarg);

		if((p = jehanne_strchr(a, '\n')) != nil)
			*p = 0;

		if(jehanne_strncmp(a, "enable", n) == 0) {
			if (waserror()) {
				jehanne_print("watchdog: enable failed\n");
				nexterror();
			}
			wd->enable();
			poperror();
			watchdogon = 1;
		} else if(jehanne_strncmp(a, "disable", n) == 0)
			wdshutdown();
		else if(jehanne_strncmp(a, "restart", n) == 0)
			wd->restart();
		else
			error(Ebadarg);
		return n;

	default:
		error(Egreg);
		break;
	}

	return 0;
}

Dev wddevtab = {
	'w',
	"watchdog",

	devreset,
	wdinit,
	wdshutdown,
	wdattach,
	wdwalk,
	wdstat,
	wdopen,
	devcreate,
	wdclose,
	wdread,
	devbread,
	wdwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
