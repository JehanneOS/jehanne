/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

int
sendmsg(int fd, char *msg)
{
	int n;

	n = jehanne_strlen(msg);
	if(write(fd, msg, n) != n)
		return -1;
	return 0;
}

static void
savelogsproc(void)
{
	int in, out, r, w;
	char buf[1024];

	out = open("/sys/log/kernel", OWRITE);
	if(out < 0){
		out = jehanne_ocreate("/sys/log/kernel", OWRITE, 0600);
		if(out < 0){
			jehanne_fprint(2, "savelogs: cannot create /sys/log/kernel: %r\n");
			return;
		}
	}

	in = open("/dev/kmesg", OREAD);
	while((r = read(in, buf, sizeof buf)) > 0){
		w = write(out, buf, r);
		if(w <= 0){
			jehanne_fprint(2, "savelogs: error writing logs: %r\n");
			return;
		}
	}
	close(in);

	in = open("/dev/kprint", OREAD);
	while((r = read(in, buf, sizeof buf)) > 0){
		w = write(out, buf, r);
		if(w <= 0){
			jehanne_fprint(2, "savelogs: error writing logs: %r\n");
			return;
		}
	}
	close(in);

	jehanne_fprint(2, "savelogs: /dev/kprint closed: %r\n");

	close(out);
}

void
savelogs(void)
{
	if(jehanne_access("/sys/log/", AEXIST) < 0){
		jehanne_fprint(2, "boot: cannot access /sys/log/kernel\n");
		return;
	}

	switch(rfork(RFPROC|RFNOWAIT|RFNOTEG|RFREND|RFFDG)){
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
	errstr(buf, sizeof buf);
	jehanne_fprint(2, "boot: %s: %s\n", s, buf);
}

void
fatal(char *s)
{
	char buf[ERRMAX];

	buf[0] = '\0';
	errstr(buf, sizeof buf);
	jehanne_fprint(2, "boot: %s: %s\n", s, buf);
	jehanne_exits(0);
}

int
readfile(char *name, char *buf, int len)
{
	int f, n;

	buf[0] = 0;
	f = open(name, OREAD);
	if(f < 0){
		jehanne_fprint(2, "readfile: cannot open %s (%r)\n", name);
		return -1;
	}
	n = read(f, buf, len-1);
	if(n >= 0)
		buf[n] = 0;
	close(f);
	return 0;
}

int
writefile(char *name, char *buf, int len)
{
	int f, n;

	f = open(name, OWRITE);
	if(f < 0)
		return -1;
	n = write(f, buf, len);
	close(f);
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
	write(f, val, jehanne_strlen(val));
	close(f);
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
	if(write(f, buf, jehanne_strlen(buf)) != jehanne_strlen(buf))
		fatal("write");
	close(f);
}

void
catchint(void *a, char *note)
{
	USED(a);
	if(jehanne_strcmp(note, "alarm") == 0)
		noted(NCONT);
	noted(NDFLT);
}

int
outin(char *prompt, char *def, int len)
{
	int n;
	char buf[256];

	if(len >= sizeof buf)
		len = sizeof(buf)-1;

	if(cpuflag){
		notify(catchint);
		alarm(15*1000);
	}
	jehanne_print("%s[%s]: ", prompt, *def ? def : "no default");
	jehanne_memset(buf, 0, sizeof buf);
	n = read(0, buf, len);

	if(cpuflag){
		alarm(0);
		notify(0);
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
		exec(rcPath, (const char**)argv);
		fatal("can't exec rc");
	default:
		break;
	}
	while(jehanne_waitpid() != -1)
		;
}
