/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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

/* Test sleep(2): notes during a sleep don't break it even if note
 * handler takes a while
 */
int verbose = 0;

int
printFirst(void *v, char *s)
{
	/* just not exit, please */
	if(strcmp(s, "alarm") == 0){
		if(verbose)
			fprint(2, "%d: noted: %s at %lld\n", getpid(), s, nsec());
		atnotify(printFirst, 0);
		sleep(2000);
		return 1;
	}
	return 0;
}

int
failOnSecond(void *v, char *s)
{
	/* just not exit, please */
	if(strcmp(s, "alarm") == 0){
		print("FAIL: timedout\n");
		exits("FAIL");
	}
	return 0;
}

void
main(void)
{
	int64_t a2000, a500, tStart, tEnd;
	if (!atnotify(printFirst, 1) || !atnotify(failOnSecond, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}

	sys_alarm(5000);
	a2000 = nsec();
	sys_alarm(500);
	a500 = nsec();
	tStart = nsec();
	sleep(1000);
	tEnd = nsec();

	if(verbose)
		fprint(2, "%d: set sys_alarm(2000)@%lld then sys_alarm(500)@%lld; elapsed in sleep() %lld nanosecond\n", getpid(), a2000, a500, tEnd-tStart);

	if((tEnd-tStart)/(1000 * 1000) > 500+2000+400 /* 400ms = acceptable error */){
		print("FAIL: should sleep less\n");
		exits("FAIL");
	}

	if((tEnd-tStart)/(1000 * 1000) < 500+2000-400  /* 400ms = acceptable error */){
		print("FAIL: should sleep more\n");
		exits("FAIL");
	}

	print("PASS\n");
	exits("PASS");
}
