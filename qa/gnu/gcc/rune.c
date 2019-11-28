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

/* Test rune assignment: should not produce any warning
 * see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67132
 */
void
main(void)
{
	int fd;
	static Rune r[16] = L"Ciao Mondo!";
	static Rune buf[16];

	if((fd = ocreate("/tmp/runetest.txt", OWRITE, 0666L)) < 0) {
		print("FAIL: open: %r\n");
		exits("FAIL");
	}
	if(jehanne_write(fd, r, sizeof(r)) < 0){
		print("FAIL: fprint: %r\n");
		exits("FAIL");
	}
	sys_close(fd);
	if((fd = sys_open("/tmp/runetest.txt", OREAD)) < 0) {
		print("FAIL: open: %r\n");
		exits("FAIL");
	}
	if(jehanne_read(fd, (char*)buf, sizeof(buf)) < 0){
		print("FAIL: read: %r\n");
		exits("FAIL");
	}
	sys_close(fd);
	if (runestrcmp(r, buf) != 0){
		print("FAIL: got '%S' instead of '%S'.\n", buf, r);
		exits("FAIL");
	} 

	print("PASS\n");
	exits("PASS");
}
