/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
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
#include <apw/stdlib.h>

int
system(const char *command)
{
	pid_t pid;
	Waitmsg *w;
	int ret = 0;
	char *s = nil;
	if(command == nil)
		return access("/cmd/rc", AEXEC) == 0;
	switch(pid = rfork(RFFDG|RFREND|RFPROC|RFENVG|RFNOTEG)){
		case -1:
			return -1;
		case 0:
			execl("/cmd/rc", "rc", "-c", command, nil);
			sysfatal("execl returned");
			return -1;
		default:
			// TODO: fix this http://man7.org/linux/man-pages/man3/system.3.html
			while((w = wait()) && w->pid != pid)
				free(w);
			if(w->msg[0] != 0){
				s = strstr(w->msg, __POSIX_EXIT_PREFIX);
				if(s){
					s += (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1);
					ret = atoi(s);
				} else
					ret = 127;
			}
			free(w);
			return ret;
	}
	
}
