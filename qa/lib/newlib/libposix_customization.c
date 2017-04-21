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
	if(status == 0){
		if(jehanne_getpid() == jehanne_getmainpid()){
			jehanne_print("PASS\n");
			return "PASS";
		}
		return nil;
	}
	jehanne_print("FAIL: " __POSIX_EXIT_PREFIX "%d\n", status);
	return "FAIL";
}

void
__application_newlib_init(void)
{
	libposix_translate_exit_status(qa_exit_translator);
}
