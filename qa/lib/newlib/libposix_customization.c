/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <u.h>
#include <libc.h>
#include <posix.h>

static char*
qa_exit_translator(int status)
{
	if(jehanne_getpid() == jehanne_getmainpid()){
		/* the QA test may fork, but only the main process
		 * should return PASS/FAIL
		 */
		if(status == 0){
			return "PASS";
		} else {
			return "FAIL";
		}
	}
	return nil;
}

void
qa_exit_printer(int status, void *_)
{
	extern int printf(const char *format, ...);
	if(jehanne_getpid() == jehanne_getmainpid()){
		/* the QA test may fork, but only the main process
		 * should return PASS/FAIL
		 */
		if(status == 0){
			printf("PASS\n");
		} else {
			printf("FAIL: " __POSIX_EXIT_PREFIX "%d\n", status);
		}
	}
}

void
__application_newlib_init(void)
{
	extern int on_exit(void (*func)(int, void*), void* arg);
	on_exit(qa_exit_printer, nil);
	libposix_translate_exit_status(qa_exit_translator);
}
