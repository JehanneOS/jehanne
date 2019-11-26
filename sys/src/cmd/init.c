/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */

#include <u.h>
#include <lib9.h>
#include <envvars.h>
#include <auth.h>
#include <authsrv.h>

char*	readfile(char *name);
char*	readenv(char*);
void	setenv(char*, char*);
void	cpenv(char*, char*);
void	closefds(void);
void	fexec(void(*)(void));
void	rcexec(void);
void	cpustart(void);
int	procopen(int pid, char *name, int mode);
void	printfile(int fd);

char	*service;
char	*cmd;
char	*cpu;
char	*systemname;
int	manual;
int	iscpu;

void
main(int argc, char *argv[])
{
	char *user;
	int fd;

	closefds();
	sys_alarm(0);

	service = "cpu";
	manual = 0;
	ARGBEGIN{
	case 'c':
		service = "cpu";
		break;
	case 'm':
		manual = 1;
		break;
	case 't':
		service = "terminal";
		break;
	}ARGEND
	cmd = *argv;

	fd = procopen(getpid(), "ctl", OWRITE);
	if(fd >= 0){
		if(jehanne_write(fd, "pri 10", 6) != 6)
			fprint(2, "init: warning: can't set priority: %r\n");
		sys_close(fd);
	}

	cpu = readenv(ENV_CPUTYPE);
	setenv(ENV_OBJTYPE, cpu);
	setenv(ENV_SERVICE, service);
	cpenv("/cfg/timezone", "timezone");
	user = readfile("#c/user");
	if(user == nil)
		user = "*unknown*";
	systemname = readfile("#c/sysname");
	if(systemname == nil)
		systemname = "*unknown*";

	newns(user, 0);
	iscpu = strcmp(service, "cpu")==0;

	if(iscpu && manual == 0)
		fexec(cpustart);

	for(;;){
		print("\ninit: starting /cmd/rc\n");
		fexec(rcexec);
		manual = 1;
		cmd = 0;
		sleep(1000);
	}
}

void
printfile(int fd)
{
	int n;
	char buf[256];
	if(fd >= 0){
		n = sys_fd2path(fd, buf, 256);
		if(n < 0){
			fprint(2, "printfile(%d): fd2path: %r\n", fd);
			return;
		}
		print("%s:\n", buf);
		while((n = jehanne_read(fd, buf, 256)) > 0){
			jehanne_write(1, buf, n);
			sleep(500);
		}
		print("\n");
	}
}

static int gotnote;

void
pinhead(void *c, char *msg)
{
	gotnote = 1;
	fprint(2, "init got note '%s'\n", msg);
	sys_noted(NCONT);
}

void
fexec(void (*execfn)(void))
{
	Waitmsg *w;
	int pid;

	switch(pid=fork()){
	case 0:
		sys_rfork(RFNOTEG);
		(*execfn)();
		print("init: exec error: %r\n");
		exits("exec");
	case -1:
		print("init: fork error: %r\n");
		exits("fork");
	default:
	casedefault:
		sys_notify(pinhead);
		gotnote = 0;
		w = wait();
		if(w == nil){
			if(gotnote)
				goto casedefault;
			print("init: wait error: %r\n");
			break;
		}
		if(w->pid != pid){
			free(w);
			goto casedefault;
		}
		if(strstr(w->msg, "exec error") != 0){
			print("init: exit string %s\n", w->msg);
			print("init: sleeping because exec failed\n");
			free(w);
			for(;;)
				sleep(1000);
		}
		if(w->msg[0])
			print("init: rc exit status: %s\n", w->msg);
		free(w);
		break;
	}
}

void
rcexec(void)
{
	if(cmd)
		execl("/cmd/rc", "rc", "-c", cmd, nil);
	else if(manual || iscpu){
		execl("/cmd/rc", "rc", "-m/arch/rc/lib/rcmain", "-i", nil);
	}else if(strcmp(service, "terminal") == 0)
		execl("/cmd/rc", "rc", "-c", ". /sys/lib/rc/startup/terminal; HOME=/usr/$USER; cd && . lib/profile", nil);
	else
		execl("/cmd/rc", "rc", nil);
}

void
cpustart(void)
{
	execl("/cmd/rc", "rc", "-c", "/cfg/startup", nil);
}

char*
readfile(char *name)
{
	int f, len;
	Dir *d;
	char *val;

	f = sys_open(name, OREAD);
	if(f < 0){
		print("init: can't open %s: %r\n", name);
		return nil;
	}
	d = dirfstat(f);
	if(d == nil){
		print("init: can't stat %s: %r\n", name);
		sys_close(f);
		return nil;
	}
	len = d->length;
	free(d);
	if(len == 0)	/* device files can be zero length but have contents */
		len = 64;
	val = malloc(len+1);
	if(val == nil){
		print("init: can't malloc %s: %r\n", name);
		sys_close(f);
		return nil;
	}
	len = jehanne_read(f, val, len);
	sys_close(f);
	if(len < 0){
		print("init: can't read %s: %r\n", name);
		return nil;
	}else
		val[len] = '\0';
	return val;
}

char*
readenv(char *name)
{
	char *val;
	char buf[128+4];

	snprint(buf, sizeof(buf), "#e/%s", name);
	val = readfile(buf);
	if(val == nil)
		val = "*unknown*";
	return val;
}

void
setenv(char *name, char *val)
{
	int fd;
	char buf[128+4];

	snprint(buf, sizeof(buf), "#e/%s", name);
	fd = ocreate(buf, OWRITE, 0644);
	if(fd < 0)
		fprint(2, "init: can't create %s: %r\n", buf);
	else{
		jehanne_write(fd, val, strlen(val));
		sys_close(fd);
	}
}

void
cpenv(char *from, char *envname)
{
	char *val;

	val = readfile(from);
	if(val != nil){
		setenv(envname, val);
		free(val);
	}
}

/*
 *  clean up after /boot
 */
void
closefds(void)
{
	int i;

	for(i = 3; i < 30; i++)
		sys_close(i);
}

int
procopen(int pid, char *name, int mode)
{
	char buf[128];
	int fd;

	snprint(buf, sizeof(buf), "#p/%d/%s", pid, name);
	fd = sys_open(buf, mode);
	if(fd < 0)
		fprint(2, "init: warning: can't open %s: %r\n", name);
	return fd;
}
