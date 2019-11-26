/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

int
sendmsg(int fd, char *msg)
{
	int n;

	n = jehanne_strlen(msg);
	if(jehanne_write(fd, msg, n) != n)
		return -1;
	return 0;
}

static void
savelogsproc(void)
{
	int in, out, r, w;
	char buf[1024];

	out = sys_open("/sys/log/kernel", OWRITE);
	if(out < 0){
		out = jehanne_ocreate("/sys/log/kernel", OWRITE, 0600);
		if(out < 0){
			jehanne_fprint(2, "savelogs: cannot create /sys/log/kernel: %r\n");
			return;
		}
	}

	in = sys_open("/dev/kmesg", OREAD);
	while((r = jehanne_read(in, buf, sizeof buf)) > 0){
		w = jehanne_write(out, buf, r);
		if(w <= 0){
			jehanne_fprint(2, "savelogs: error writing logs: %r\n");
			return;
		}
	}
	sys_close(in);

	in = sys_open("/dev/kprint", OREAD);
	while((r = jehanne_read(in, buf, sizeof buf)) > 0){
		w = jehanne_write(out, buf, r);
		if(w <= 0){
			jehanne_fprint(2, "savelogs: error writing logs: %r\n");
			return;
		}
	}
	sys_close(in);

	jehanne_fprint(2, "savelogs: /dev/kprint closed: %r\n");

	sys_close(out);
}

void
savelogs(void)
{
	if(jehanne_access("/sys/log/", AEXIST) < 0){
		jehanne_fprint(2, "boot: cannot access /sys/log/kernel\n");
		return;
	}

	switch(sys_rfork(RFPROC|RFNOWAIT|RFNOTEG|RFREND|RFFDG)){
	case -1:
		jehanne_print("boot: savelogs: fork failed: %r\n");
	case 0:
		savelogsproc();
	default:
		break;
	}
}

void
warning(char *s)
{
	char buf[ERRMAX];

	buf[0] = '\0';
	sys_errstr(buf, sizeof buf);
	jehanne_fprint(2, "boot: %s: %s\n", s, buf);
}

void
fatal(char *s)
{
	char buf[ERRMAX];

	buf[0] = '\0';
	sys_errstr(buf, sizeof buf);
	jehanne_fprint(2, "boot: %s: %s\n", s, buf);
	jehanne_exits(0);
}

int
readfile(char *name, char *buf, int len)
{
	int f, n;

	buf[0] = 0;
	f = sys_open(name, OREAD);
	if(f < 0){
		jehanne_fprint(2, "readfile: cannot open %s (%r)\n", name);
		return -1;
	}
	n = jehanne_read(f, buf, len-1);
	if(n >= 0)
		buf[n] = 0;
	sys_close(f);
	return 0;
}

int
writefile(char *name, char *buf, int len)
{
	int f, n;

	f = sys_open(name, OWRITE);
	if(f < 0)
		return -1;
	n = jehanne_write(f, buf, len);
	sys_close(f);
	return (n != len) ? -1 : 0;
}

void
setenv(char *name, char *val)
{
	int f;
	char ename[64];

	jehanne_snprint(ename, sizeof ename, "#e/%s", name);
	f = jehanne_ocreate(ename, OWRITE, 0666);
	if(f < 0){
		jehanne_fprint(2, "create %s: %r\n", ename);
		return;
	}
	jehanne_write(f, val, jehanne_strlen(val));
	sys_close(f);
}

void
srvcreate(char *name, int fd)
{
	char *srvname;
	int f;
	char buf[64];

	srvname = jehanne_strrchr(name, '/');
	if(srvname)
		srvname++;
	else
		srvname = name;

	jehanne_snprint(buf, sizeof buf, "#s/%s", srvname);
	f = jehanne_ocreate(buf, OWRITE, 0600);
	if(f < 0)
		fatal(buf);
	jehanne_sprint(buf, "%d", fd);
	if(jehanne_write(f, buf, jehanne_strlen(buf)) != jehanne_strlen(buf))
		fatal("write");
	sys_close(f);
}

void
catchint(void *a, char *note)
{
	USED(a);
	if(jehanne_strcmp(note, "alarm") == 0)
		sys_noted(NCONT);
	sys_noted(NDFLT);
}

int
outin(char *prompt, char *def, int len)
{
	int n;
	char buf[256];

	if(len >= sizeof buf)
		len = sizeof(buf)-1;

	if(cpuflag){
		sys_notify(catchint);
		sys_alarm(15*1000);
	}
	jehanne_print("%s[%s]: ", prompt, *def ? def : "no default");
	jehanne_memset(buf, 0, sizeof buf);
	n = jehanne_read(0, buf, len);

	if(cpuflag){
		sys_alarm(0);
		sys_notify(0);
	}

	if(n < 0){
		return 1;
	}
	if (n > 1) {
		jehanne_strncpy(def, buf, len);
	}
	return n;
}

void shell(char *c, char *d)
{
	char *argv[] = {"rc", "-m", rcmainPath, 0, 0, 0};
	argv[3] = c;
	argv[4] = d;

	if(jehanne_access(rcPath, AEXEC) < 0)
		fatal("cannot find rc");

	switch(jehanne_fork()){
	case -1:
		jehanne_print("configrc: fork failed: %r\n");
	case 0:
		sys_exec(rcPath, (const char**)argv);
		fatal("can't exec rc");
	default:
		break;
	}
	while(jehanne_waitpid() != -1)
		;
}
