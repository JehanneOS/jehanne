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

void
handler(void *v, char *s)
{
	int64_t wakeup;
	wakeup = awake(1000);
	while(rendezvous(&wakeup, (void*)1) == (void*)~0)
		if(jehanne_awakened(wakeup)){
			print("PASS\n");
			exits(nil);
		}
	print("FAIL in note handler\n");
	exits("FAIL");
}

void
main(int argc, char**argv)
{
	int fd, i;
	if(argc > 1){
		fd = ocreate(argv[1], OWRITE, 0666);
		dup(fd, 1);
		close(fd);
	}

	if (notify(handler)){
		fprint(2, "%r\n");
		exits("notify fails");
	}

	print("%s %d: waiting for note", argv[0], getpid());
	for(i = 0; i < 10*1000*1000; ++i)
		if(i % 4999 == 0){
			sleep(100);
			print(".");
		}

	print("FAIL\n");
	exits("FAIL");
}
