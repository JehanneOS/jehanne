/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include <envvars.h>
#include <auth.h>
#include <9P2000.h>
#include "../boot/boot.h"

char	cputype[64];
char	sys[2*64];
int	printcol;
int	mflag;
int	fflag;
int	kflag;

char	*bargv[Nbarg];
int	bargc;

static Method	*rootserver(char*);
static void	usbinit(void);
static int	startconsole(void);
static int	startcomconsole(void);
static void	bindBoot(void);
static void	unbindBoot(void);
static void	kbmap(void);

void
boot(int argc, char *argv[])
{
	int fd, afd;
	Method *mp;
	char *cmd, cmdbuf[64], *iargv[16];
	char rootbuf[64];
	int islocal, ishybrid;
	char *rp, *rsp;
	int iargc, n;
	char buf[32];
	AuthInfo *ai;

	open("/dev/cons", OREAD);
	open("/dev/cons", OWRITE);
	open("/dev/cons", OWRITE);

	jehanne_fmtinstall('r', jehanne_errfmt);

	bindBoot();

	/*
	 *  start /dev/cons
	 */
	if(readfile("#ec/bootconsole", buf, sizeof(buf)) >= 0
	&& jehanne_strcmp("comconsole", buf) == 0){
		if(startcomconsole() < 0)
			fatal("no console found");
	} else if(startconsole() < 0){
		if(startcomconsole() < 0)
			fatal("no console found");
	}

	/*
	 * init will reinitialize its namespace.
	 * #ec gets us plan9.ini settings (*var variables).
	 */
	bind("#ec", "/env", MREPL);
	bind("#e", "/env", MBEFORE|MCREATE);
	bind("#s", "/srv", MREPL|MCREATE);
	bind("#p", "/proc", MREPL|MCREATE);
	bind("#σ", "/shr", MREPL);
	jehanne_print("Diex vos sait! Je m'appelle Jehanne O:-)\n");
#ifdef DEBUG
	jehanne_print("argc=%d\n", argc);
	for(fd = 0; fd < argc; fd++)
		jehanne_print("%#p %s ", argv[fd], argv[fd]);
	jehanne_print("\n");
#endif //DEBUG

	ARGBEGIN{
	case 'k':
		kflag = 1;
		break;
	case 'm':
		mflag = 1;
		break;
	case 'f':
		fflag = 1;
		break;
	}ARGEND
	readfile("#e/" ENV_CPUTYPE, cputype, sizeof(cputype));

	/*
	 *  set up usb keyboard, mouse and disk, if any.
	 */
	usbinit();

	/*
	 *  pick a method and initialize it
	 */
	if(method[0].name == nil)
		fatal("no boot methods");
	mp = rootserver(argc ? *argv : 0);
	(*mp->config)(mp);
	islocal = jehanne_strcmp(mp->name, "local") == 0;
	ishybrid = jehanne_strcmp(mp->name, "hybrid") == 0;

	/*
	 *  load keymap if it is there.
	 */
	kbmap();

	/*
 	 *  authentication agent
	 */
	authentication(cpuflag);

jehanne_print("connect...");
	/*
	 *  connect to the root file system
	 */
	fd = (*mp->connect)();
	if(fd < 0)
		fatal("can't connect to file server");
	if(!islocal && !ishybrid){
		if(cfs)
			fd = (*cfs)(fd);
	}
jehanne_print("\n");

	jehanne_print("version...");
	buf[0] = '\0';
	n = fversion(fd, 0, buf, sizeof buf);
	if(n < 0)
		fatal("can't init 9P");

	if(jehanne_access("#s/boot", AEXIST) < 0)
		srvcreate("boot", fd);

	unbindBoot();

	/*
	 *  create the name space, mount the root fs
	 */
	if(bind("/", "/", MREPL) < 0)
		fatal("bind /");
	rp = jehanne_getenv("rootspec");
	if(rp == nil)
		rp = "";

	afd = fauth(fd, rp);
	if(afd >= 0){
		ai = auth_proxy(afd, auth_getkey, "proto=p9any role=client");
		if(ai == nil)
			jehanne_print("authentication failed (%r), trying mount anyways\n");
	}
	if(mount(fd, afd, "/root", MREPL|MCREATE, rp, '9') < 0)
		fatal("mount /");
	rsp = rp;
	rp = jehanne_getenv("rootdir");
	if(rp == nil)
		rp = rootdir;
	if(bind(rp, "/", MAFTER|MCREATE) < 0){
		if(jehanne_strncmp(rp, "/root", 5) == 0){
			jehanne_fprint(2, "boot: couldn't bind $rootdir=%s to root: %r\n", rp);
			fatal("second bind /");
		}
		jehanne_snprint(rootbuf, sizeof rootbuf, "/root/%s", rp);
		rp = rootbuf;
		if(bind(rp, "/", MAFTER|MCREATE) < 0){
			jehanne_fprint(2, "boot: couldn't bind $rootdir=%s to root: %r\n", rp);
			if(jehanne_strcmp(rootbuf, "/root//plan9") == 0){
				jehanne_fprint(2, "**** warning: remove rootdir=/plan9 entry from plan9.ini\n");
				rp = "/root";
				if(bind(rp, "/", MAFTER|MCREATE) < 0)
					fatal("second bind /");
			}else
				fatal("second bind /");
		}
	}
	close(fd);
	setenv("rootdir", rp);

	savelogs();

	settime(islocal, afd, rsp);
	if(afd > 0)
		close(afd);

	cmd = jehanne_getenv("init");
	if(cmd == nil){
		jehanne_sprint(cmdbuf, "/arch/%s/cmd/init -%s%s", cputype,
			cpuflag ? "c" : "t", mflag ? "m" : "");
		cmd = cmdbuf;
	}
	iargc = jehanne_tokenize(cmd, iargv, nelem(iargv)-1);
	cmd = iargv[0];

	/* make iargv[0] basename(iargv[0]) */
	if(iargv[0] = jehanne_strrchr(iargv[0], '/'))
		iargv[0]++;
	else
		iargv[0] = cmd;

	iargv[iargc] = nil;

	exec(cmd, (const char**)iargv);
	fatal(cmd);
}

static Method*
findmethod(char *a)
{
	Method *mp;
	int i, j;
	char *cp;

	if((i = jehanne_strlen(a)) == 0)
		return nil;
	cp = jehanne_strchr(a, '!');
	if(cp)
		i = cp - a;
	for(mp = method; mp->name; mp++){
		j = jehanne_strlen(mp->name);
		if(j > i)
			j = i;
		if(jehanne_strncmp(a, mp->name, j) == 0)
			break;
	}
	if(mp->name)
		return mp;
	return nil;
}

/*
 *  ask user from whence cometh the root file system
 */
static Method*
rootserver(char *arg)
{
	char prompt[256];
	int rc;
	Method *mp;
	char *cp;
	char reply[256];
	int n;

	/* look for required reply */
	rc = readfile("#ec/nobootprompt", reply, sizeof(reply));
	if(rc == 0 && reply[0]){
		mp = findmethod(reply);
		if(mp)
			goto HaveMethod;
		jehanne_print("boot method %s not found\n", reply);
		reply[0] = 0;
	}

	/* make list of methods */
	mp = method;
	n = jehanne_sprint(prompt, "root is from (%s", mp->name);
	for(mp++; mp->name; mp++)
		n += jehanne_sprint(prompt+n, ", %s", mp->name);
	jehanne_sprint(prompt+n, ")");

	/* create default reply */
	readfile("#ec/bootargs", reply, sizeof(reply));
	if(reply[0] == 0 && arg != 0)
		jehanne_strcpy(reply, arg);
	if(reply[0]){
		mp = findmethod(reply);
		if(mp == 0)
			reply[0] = 0;
	}
	if(reply[0] == 0)
		jehanne_strcpy(reply, method->name);

	/* parse replies */
	do{
		outin(prompt, reply, sizeof(reply));
		mp = findmethod(reply);
	}while(mp == nil);

HaveMethod:
	bargc = jehanne_tokenize(reply, bargv, Nbarg-2);
	bargv[bargc] = nil;
	cp = jehanne_strchr(reply, '!');
	if(cp)
		jehanne_strcpy(sys, cp+1);
	return mp;
}

static void
usbinit(void)
{
	Waitmsg *w;
	static char *argv[] = {"usbrc", nil};
	int pid;

	if (jehanne_access(usbrcPath, AEXIST) < 0) {
		jehanne_print("usbinit: no %s\n", usbrcPath);
		return;
	}

	switch(pid = jehanne_fork()){
	case -1:
		jehanne_print("usbinit: fork failed: %r\n");
	case 0:
		exec(usbrcPath, (const char**)argv);
		fatal("can't exec usbd");
	default:
		break;
	}
	jehanne_print("usbinit: waiting usbrc...");
	for(;;){
		w = jehanne_wait();
		if(w != nil && w->pid == pid){
			if(w->msg[0] != 0)
				fatal(w->msg);
			jehanne_free(w);
			break;
		} else if(w == nil) {
			fatal("configuring usbinit");
		} else if(w->msg[0] != 0){
			jehanne_print("usbinit: wait: %d %s\n", w->pid, w->msg);
		}
		jehanne_free(w);
	}
	jehanne_print("done\n");
}

static int
startconsole(void)
{
	char *dbgfile, *argv[16], **av;
	int i;

	if(jehanne_access(screenconsolePath, AEXEC) < 0){
		jehanne_print("cannot find screenconsole: %r\n");
		return -1;
	}

	/* start agent */
	i = 0;
	av = argv;
	av[i++] = "screenconsole";
	if(dbgfile = jehanne_getenv("debugconsole")){
		av[i++] = "-d";
		av[i++] = dbgfile;
	}
	av[i] = 0;
	switch(jehanne_fork()){
	case -1:
		fatal("starting screenconsole");
	case 0:
		exec(screenconsolePath, (const char**)av);
		fatal("execing screenconsole");
	default:
		break;
	}

	/* wait for agent to really be there */
	while(jehanne_access("#s/screenconsole", AEXIST) < 0){
		jehanne_sleep(250);
	}
	/* replace 0, 1 and 2 */
	if((i = open("#s/screenconsole", ORDWR)) < 0)
		fatal("open #s/screenconsole");
	if(mount(i, -1, "/dev", MBEFORE, "", '9') < 0)
		fatal("mount /dev");
	if((i = open("/dev/cons", OREAD))<0)
		fatal("open /dev/cons, OREAD");
	if(jehanne_dup(i, 0) != 0)
		fatal("jehanne_dup(i, 0)");
	close(i);
	if((i = open("/dev/cons", OWRITE))<0)
		fatal("open /dev/cons, OWRITE");
	if(jehanne_dup(i, 1) != 1)
		fatal("jehanne_dup(i, 1)");
	close(i);
	if(jehanne_dup(1, 2) != 2)
		fatal("jehanne_dup(1, 2)");
	return 0;
}

static int
startcomconsole(void)
{
	char *dbgfile, *argv[16], **av;
	int i;

	if(jehanne_access(comconsolePath, AEXEC) < 0){
		jehanne_print("cannot find comconsole: %r\n");
		return -1;
	}

	/* start agent */
	i = 0;
	av = argv;
	av[i++] = "comconsole";
	if(dbgfile = jehanne_getenv("debugconsole")){
		av[i++] = "-d";
		av[i++] = dbgfile;
	}
	av[i++] = "-s";
	av[i++] = "comconsole";
	av[i++] = "#t/eia0";
	av[i] = 0;
	switch(jehanne_fork()){
	case -1:
		fatal("starting comconsole");
	case 0:
		exec(comconsolePath, (const char**)av);
		fatal("execing comconsole");
	default:
		break;
	}

	/* wait for agent to really be there */
	while(jehanne_access("#s/comconsole", AEXIST) < 0){
		jehanne_sleep(250);
	}
	/* replace 0, 1 and 2 */
	if((i = open("#s/comconsole", ORDWR)) < 0)
		fatal("open #s/comconsole");
	if(mount(i, -1, "/dev", MBEFORE, "", '9') < 0)
		fatal("mount /dev");
	if((i = open("/dev/cons", OREAD))<0)
		fatal("open /dev/cons, OREAD");
	if(jehanne_dup(i, 0) != 0)
		fatal("jehanne_dup(i, 0)");
	close(i);
	if((i = open("/dev/cons", OWRITE))<0)
		fatal("open /dev/cons, OWRITE");
	if(jehanne_dup(i, 1) != 1)
		fatal("jehanne_dup(i, 1)");
	close(i);
	if(jehanne_dup(1, 2) != 2)
		fatal("jehanne_dup(1, 2)");
	return 0;
}

static void
bindBoot(void)
{
	BootBind *b = bootbinds;

	if(b == nil || b->name == nil)
		return;
	while(b->name){
		bind(b->name, b->old, b->flag);
		++b;
	}
}

static void
unbindBoot(void)
{
	BootBind *b = bootbinds;

	if(b == nil || b->name == nil)
		return;
	while(b->name)
		++b;

	while(--b >= bootbinds){
		unmount(b->name, b->old);
	}
}

static void
kbmap(void)
{
	char *f;
	int n, in, out;
	char buf[1024];

	f = jehanne_getenv("kbmap");
	if(f == nil)
		return;
	if(bind("#κ", "/dev", MAFTER) < 0){
		warning("can't bind #κ");
		return;
	}

	in = open(f, OREAD);
	if(in < 0){
		warning("can't open kbd map");
		return;
	}
	out = open("/dev/kbmap", OWRITE);
	if(out < 0) {
		warning("can't open /dev/kbmap");
		close(in);
		return;
	}
	while((n = read(in, buf, sizeof(buf))) > 0)
		if(write(out, buf, n) != n){
			warning("write to /dev/kbmap failed");
			break;
		}
	close(in);
	close(out);
}
