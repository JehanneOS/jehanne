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
#include "awk.h"

#define MAXFORKS	20
#define NSYSFILE	3
#define	tst(a,b)	(mode == OREAD? (b) : (a))
#define	RDR	0
#define	WTR	1

struct a_fork {
	short	done;
	short	fd;
	int	pid;
	char status[128];
};
static struct a_fork the_fork[MAXFORKS];

Biobuf*
popen(char *cmd, int mode)
{
	int p[2];
	int myside, hisside, pid;
	int i, ind;

	for (ind = 0; ind < MAXFORKS; ind++)
		if (the_fork[ind].pid == 0)
			break;
	if (ind == MAXFORKS)
		return nil;
	if(pipe(p) < 0)
		return nil;
	myside = tst(p[WTR], p[RDR]);
	hisside = tst(p[RDR], p[WTR]);
	switch (pid = fork()) {
	case -1:
		return nil;
	case 0:
		/* myside and hisside reverse roles in child */
		sys_close(myside);
		dup(hisside, tst(0, 1));
		for (i=NSYSFILE; i<FOPEN_MAX; i++)
			sys_close(i);
		execl("/bin/rc", "rc", "-c", cmd, nil);
		exits("exec failed");
	default:
		the_fork[ind].pid = pid;
		the_fork[ind].fd = myside;
		the_fork[ind].done = 0;
		sys_close(hisside);
		return(Bfdopen(myside, mode));
	}
}

int
pclose(Biobuf *ptr)
{
	int f, r, ind;
	Waitmsg *status;

	f = Bfildes(ptr);
	Bterm(ptr);
	for (ind = 0; ind < MAXFORKS; ind++)
		if (the_fork[ind].fd == f && the_fork[ind].pid != 0)
			break;
	if (ind == MAXFORKS)
		return -1;
	if (!the_fork[ind].done) {
		do {
			if((status = wait()) == nil)
				r = -1;
			else
				r = status->pid;
			for (f = 0; f < MAXFORKS; f++) {
				if (r == the_fork[f].pid) {
					the_fork[f].done = 1;
					strecpy(the_fork[f].status, the_fork[f].status+512, status->msg);
					break;
				}
			}
			free(status);
		} while(r != the_fork[ind].pid && r != -1);
		if(r == -1)
			strcpy(the_fork[ind].status, "No loved ones to wait for");
	}
	the_fork[ind].pid = 0;
	if(the_fork[ind].status[0] != '\0')
		return 1;
	return 0;
}
