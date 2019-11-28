/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <lib9.h>

int verbose = 1;

int
failOnTimeout(void *v, char *s)
{
	if(strncmp(s, "alarm", 4) == 0){
		if(verbose)
			print("%d: noted: %s\n", getpid(), s);
		print("FAIL: timeout\n");
		exits("FAIL");
	}
	return 0;
}

int
writeTillBlock(int fd)
{
	int i = 0;
	char buf[1025];
	memset(buf, 1, sizeof(buf));
	while(i < 300)	/* pipes should block after at most 256kb */
	{
		if(jehanne_write(fd, buf, sizeof(buf)) < 0){
			break;
		}
		++i;
	}
	return i;
}

void
main(void)
{
	int64_t start, elapsed, wkup, res;
	int32_t sem = 0;
	int fds[2];
	char buf[1];

	sys_semacquire(&sem, 0);

	sys_alarm(40000);	/* global timeout, FAIL if reached */
	if (!atnotify(failOnTimeout, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	sys_awake(100000);	/* this will be handled by the kernel, see pexit */

	/* verify that rendezvous are interrupted */
	fprint(2, "verify that rendezvous are interrupted\n", elapsed);
	wkup = sys_awake(1000);
	start = nsec();
	res = (int64_t)sys_rendezvous(&elapsed, (void*)0x12345);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		fprint(2, "rendezvous interrupted, returned %#p, elapsed = %d ms\n", res, elapsed);
	if(!awakened(wkup) || elapsed < 900 || elapsed > 1800){
		print("FAIL: rendezvous\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* verify that sleeps are NOT interrupted */
	fprint(2, "verify that sleeps are NOT interrupted\n", elapsed);
	wkup = sys_awake(700);
	start = nsec();
	sleep(1500);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		fprint(2, "sleep(1500), elapsed = %d ms\n", elapsed);
	if(elapsed < 1300){
		print("FAIL: sleep\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* verify that semacquires are interrupted */
	fprint(2, "verify that semacquires are interrupted\n", elapsed);
	pipe(fds);
	wkup = sys_awake(1000);
	start = nsec();
	if(verbose)
		print("semacquire(&sem, 1)...\n");
	res = sys_semacquire(&sem, 1);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		print("semacquire(&sem, 1): returned %lld, elapsed = %d ms\n", res, elapsed);
	if(!awakened(wkup) || res != -1 || elapsed < 900 || elapsed > 1800){
		print("FAIL: semacquire\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* verify that tsemacquire are NOT interrupted */
	fprint(2, "verify that tsemacquire are NOT interrupted\n", elapsed);
	start = nsec();
	wkup = sys_awake(500);
	res = tsemacquire(&sem, 1500);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		fprint(2, "tsemacquire(&sem, 1500) returned %lld, elapsed = %d ms\n", res, elapsed);
	if(res != 0 || elapsed < 1300){
		print("FAIL: tsemacquire\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* verify that reads are interrupted */
	fprint(2, "verify that reads are interrupted\n", elapsed);
	if(pipe(fds) < 0){
		print("FAIL: pipe\n");
		exits("FAIL");
	}
	wkup = sys_awake(1000);
	start = nsec();
	res = jehanne_read(fds[0], buf, 1);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		fprint(2, "read(fds[0], buf, 1) returned %lld, elapsed = %d ms\n", res, elapsed);
	if(!awakened(wkup) || res != -1 || elapsed < 900 || elapsed > 1800){
		print("FAIL: read\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* verify that writes are interrupted */
	fprint(2, "verify that writes are interrupted\n", elapsed);
	if(pipe(fds) < 0){
		print("FAIL: pipe\n");
		exits("FAIL");
	}
	wkup = sys_awake(1000);
	start = nsec();
	res = writeTillBlock(fds[0]);
	elapsed = (nsec() - start) / (1000 * 1000);
	if(verbose)
		fprint(2, "writeTillBlock(fds[0]) returned %lld, elapsed = %d ms\n", res, elapsed);
	if(!awakened(wkup) || res >= 256 || elapsed < 900 || elapsed > 1800){
		print("FAIL: write\n");
		exits("FAIL");
	}
	forgivewkp(wkup);

	/* do not forgivewkp the awake(100000): the kernel must handle it */
	print("PASS\n");
	exits("PASS");
}
