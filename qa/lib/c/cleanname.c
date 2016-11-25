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
#include <u.h>
#include <libc.h>

void
main(void)
{
	char buf[256], *s;

	strcpy(buf, "#abcde");
	s = cleanname(buf);
	if(s != buf || strcmp("#abcde", buf)){
		print("FAIL, cleanname(#abcde) = %s\n", s);
		exits("FAIL");
	}
	strcpy(buf, "#abcde/fg/../hi");
	s = cleanname(buf);
	if(s != buf || strcmp("#abcde/hi", buf)){
		print("FAIL, cleanname(#abcde/fg/../hi) = %s\n", s);
		exits("FAIL");
	}
	strcpy(buf, "#abcde/fg/./hi/");
	s = cleanname(buf);
	if(s != buf || strcmp("#abcde/fg/hi", buf)){
		print("FAIL, cleanname(#abcde/fg/./hi) = %s\n", s);
		exits("FAIL");
	}

	print("PASS\n");
	exits("PASS");
}
