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

int done;
int waited;

void
handler(void *v, char *s)
{
	int i;
	if(strcmp(s, "stop") == 0){
		done = 1;
		print("stop note received; done = %d\n", done);
	}else{
		print("waiting after %s", s);
		for(i = 0; i < 1000*1000; ++i)
			if(i % 4999 == 0)
				print(".");
		print("\n");
		print("wait after %s terminated\n", s);
		waited++;
	}
	sys_noted(NCONT);
}

void
main(int argc, char**argv)
{
	int fd;
	if(argc > 1){
		fd = ocreate(argv[1], OWRITE, 0666);
		dup(fd, 1);
		sys_close(fd);
	}

	if (sys_notify(handler)){
		fprint(2, "%r\n");
		exits("sys_notify fails");
	}

	print("note handler installed\n");

	while(!done)
		sleep(100);
	if(waited == 2){
		print("PASS\n");
		exits("PASS");
	}
	print("%d notes received\n", waited);
	exits("FAIL");
}
