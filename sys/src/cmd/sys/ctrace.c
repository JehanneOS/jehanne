/* Copyright (c) 20XX 9front
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <u.h>
#include <lib9.h>
#include <thread.h>

enum {
	Stacksize	= 8*1024,
};

Channel *out;
Channel *quit;
Channel *forkc;
int readers = 1;
int output;

typedef struct Msg Msg;
struct Msg {
	int	pid;
	char	buf[8*1024];
};

typedef struct Reader Reader;
struct Reader {
	int	pid;

	int	tfd;
	int	cfd;

	Msg*	msg;
};

void
die(Reader *r)
{
	Msg *s;

	s = r->msg;
	snprint(s->buf, sizeof(s->buf), " = %r\n");
	s->pid = r->pid;
	sendp(quit, s);
	if(r->tfd >= 0)
		sys_close(r->tfd);
	if(r->cfd >= 0)
		sys_close(r->cfd);
	threadexits(nil);
}

void
cwrite(Reader *r, char *cmd, int ignore_errors)
{
	if (jehanne_write(r->cfd, cmd, strlen(cmd)) < 0 && !ignore_errors)
		die(r);
}

void
reader(void *v)
{
	int newpid, n;
	Reader r;
	Msg *s;
	char *rf, *a[16];
	long wakeup;

	r.pid = (int)(uintptr)v;
	r.tfd = r.cfd = -1;

	r.msg = s = mallocz(sizeof(Msg), 1);
	snprint(s->buf, sizeof(s->buf), "/proc/%d/ctl", r.pid);
	if ((r.cfd = sys_open(s->buf, OWRITE)) < 0)
		die(&r);
	snprint(s->buf, sizeof(s->buf), "/proc/%d/syscall", r.pid);
	if ((r.tfd = sys_open(s->buf, OREAD)) < 0)
		die(&r);

StartReading:
	cwrite(&r, "stop", 0);
	cwrite(&r, "startsyscall", 0);

	wakeup = sys_awake(750);
	while((n = sys_pread(r.tfd, s->buf, sizeof(s->buf)-1, 0)) > 0){
		forgivewkp(wakeup);
		if(strstr(s->buf, " rfork ") != nil){
			rf = strdup(s->buf);
			if(tokenize(rf, a, 9) == 9
			&& a[4][0] == '<'
			&& a[5][0] != '-'
			&& strcmp(a[2], "rfork") == 0) {
				newpid = strtol(a[5], 0, 0);
				if(newpid){
					sendp(forkc, (void*)(uintptr_t)newpid);
					procrfork(reader, (void*)(uintptr_t)newpid, Stacksize, 0);
				}
			}
			free(rf);
		}

		s->pid = r.pid;
		sendp(out, s);

		r.msg = s = mallocz(sizeof(Msg), 1);
		cwrite(&r, "startsyscall", 1);
		wakeup = sys_awake(500);
	}
	if(awakened(wakeup)){
		rf = smprint("/proc/%d/status", r.pid);
		if(access(rf, AEXIST) == 0){
			free(rf);
			goto StartReading;
		}
		free(rf);
	}
	die(&r);
}

void
writer(int lastpid)
{
	char lastc = -1;
	Alt a[4];
	Msg *s;
	int n;

	a[0].op = CHANRCV;
	a[0].c = quit;
	a[0].v = &s;
	a[1].op = CHANRCV;
	a[1].c = out;
	a[1].v = &s;
	a[2].op = CHANRCV;
	a[2].c = forkc;
	a[2].v = nil;
	a[3].op = CHANEND;

	while(readers > 0){
		switch(alt(a)){
		case 0:
			readers--;
		case 1:
			if(s->pid != lastpid){
				lastpid = s->pid;
				if(lastc != '\n'){
					lastc = '\n';
					jehanne_write(output, &lastc, 1);
				}
				if(s->buf[1] == '=')
					fprint(output, "%d ...", lastpid);
			}
			n = strlen(s->buf);
			if(n > 0){
				jehanne_write(output, s->buf, n);
				lastc = s->buf[n-1];
			}
			free(s);
			break;
		case 2:
			readers++;
			break;
		}
	}
}

void
usage(void)
{
	fprint(2, "Usage: sys/ctrace [-o file] [pid] | [-c cmd [arg...]]\n");
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int pid;
	char *cmd = nil;
	char **args = nil;

	/*
	 * don't bother with fancy arg processing, because it picks up options
	 * for the command you are starting.
	 * Just check for -o and -c as initial arguments and then take it from there.
	 */
	if (argc < 2)
		usage();
ParseArguments:
	if (argv[1][0] == '-'){
		switch(argv[1][1]) {
		case 'c':
			if (argc < 3)
				usage();
			cmd = strdup(argv[2]);
			args = &argv[2];
			break;
		case 'o':
			if (argc < 4)	// sys/ctrace -o file pid
				usage();
			if (output)	// -o can only appear once
				usage();
			output = ocreate(argv[2], OWRITE|OCEXEC, 0660);
			if(output < 0){
				fprint(2, "sys/ctrace: cannot open %d: %r\n", argv[2]);
				threadexits("output");
			}
			argc -= 2;
			argv += 2;
			goto ParseArguments;
		default:
			usage();
		}
	}

	if(!output)
		output = 2;

	/* run a command? */
	if(cmd) {
		pid = fork();
		if (pid < 0)
			sysfatal("fork failed: %r");
		if(pid == 0) {
			jehanne_write(sys_open(smprint("/proc/%d/ctl", getpid()), OWRITE|OCEXEC), "hang", 4);
			sys_exec(cmd, (const char**)args);
			if(cmd[0] != '/')
				sys_exec(smprint("/cmd/%s", cmd), (const char**)args);
			sysfatal("exec %s failed: %r", cmd);
		}
	} else {
		if(argc != 2)
			usage();
		pid = atoi(argv[1]);
	}

	out   = chancreate(sizeof(Msg*), 0);
	quit  = chancreate(sizeof(Msg*), 0);
	forkc = chancreate(sizeof(void*), 0);
	procrfork(reader, (void*)(uintptr_t)pid, Stacksize, 0);

	writer(pid);
	threadexits(nil);
}
