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
#include <bio.h>

typedef struct Proc Proc;
struct Proc {
	int pid;
	Proc *first, *parent, *next;
	Proc *hash;
};

Proc	*hash[1024];
Rune	buf[512];
Biobuf	bout;

Proc *
getproc(int pid)
{
	Proc *p;

	for(p = hash[pid % nelem(hash)]; p; p = p->hash)
		if(p->pid == pid)
			return p;
	return nil;
}

void
addproc(int pid)
{
	Proc *p;
	
	p = mallocz(sizeof(*p), 1);
	if(p == nil)
		sysfatal("malloc: %r");
	p->pid = pid;
	p->hash = hash[pid % nelem(hash)];
	hash[pid % nelem(hash)] = p;
}

int
theppid(int pid)
{
	char b[128];
	int fd, ppid;
	
	ppid = 0;
	snprint(b, sizeof(b), "%d/ppid", pid);
	fd = open(b, OREAD);
	if(fd >= 0){
		memset(b, 0, sizeof b);
		if(read(fd, b, sizeof b-1) >= 0){
			ppid = atoi(b);
			if(ppid < 0)
				ppid = 0;
		}
		close(fd);
	}
	return ppid;
}

void
addppid(int pid)
{
	Proc *p, *par, **l;
	
	p = getproc(pid);
	par = getproc(theppid(pid));
	if(par == nil)
		par = getproc(0);
	p->parent = par;
	for(l = &par->first; *l; l = &((*l)->next))
		if((*l)->pid > pid)
			break;
	p->next = *l;
	*l = p;
}

void
addprocs(void)
{
	int fd, rc, i;
	Dir *d;
	
	fd = open(".", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	rc = dirreadall(fd, &d);
	if(rc < 0)
		sysfatal("dirreadall: %r");
	close(fd);
	for(i = 0; i < rc; i++)
		if(d[i].mode & DMDIR)
			addproc(atoi(d[i].name));
	for(i = 0; i < rc; i++)
		if(d[i].mode & DMDIR)
			addppid(atoi(d[i].name));
	free(d);
}

int
readout(char *file)
{
	int fd, rc, i, n;
	char b[512];

	fd = open(file, OREAD);
	if(fd < 0)
		return -1;
	n = 0;
	while((rc = read(fd, b, sizeof b)) > 0){
		for(i=0; i<rc; i++)
			if(b[i] == '\n')
				b[i] = ' ';
		n += Bwrite(&bout, b, rc);
	}
	close(fd);
	return n;
}

void
printargs(int pid)
{
	char b[128], *p;
	int fd;

	snprint(b, sizeof(b), "%d/args", pid);
	if(readout(b) > 0)
		return;
	snprint(b, sizeof(b), "%d/status", pid);
	fd = open(b, OREAD);
	if(fd >= 0){
		memset(b, 0, sizeof b);
		if(read(fd, b, 27) > 0){
			p = b + strlen(b);
			while(p > b && p[-1] == ' ')
				*--p = 0;
			Bprint(&bout, "%s", b);
		}
		close(fd);
	}
}

void
descend(Proc *p, Rune *r)
{
	Rune last;
	Proc *q;
	
	last = *--r;
	*r = last == L' ' ? L'└' : L'├';
	if(p->pid != 0){
		Bprint(&bout, "%-11d %S", p->pid, buf);
		printargs(p->pid);
		Bprint(&bout, "\n");
	}
	*r = last;
	*++r = L'│';
	for(q = p->first; q; q = q->next) {
		if(q->next == nil)
			*r = L' ';
		descend(q, r + 1);
	}
	*r = 0;
}

void
printprocs(void)
{
	descend(getproc(0), buf);
}

void
main()
{
	Binit(&bout, 1, OWRITE);
	if(chdir("/proc")==-1)
		sysfatal("chdir: %r");

	addproc(0);
	addprocs();
	printprocs();

	exits(0);
}
