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
#include <libc.h>

static int
openprocnotes(int pid)
{
	char file[128];
	int32_t f;

	sprint(file, "#p/%d/note", pid);
	f = open(file, OWRITE);
	if(f < 0){
		sprint(file, "/proc/%d/note", pid);
		f = open(file, OWRITE);
	}
	if(f < 0)
		werrstr("postnote: cannot open neither #p/%d/note nor /proc/%d/note", pid, pid);
	return f;
}

static int
opengroupnotes(int pid)
{
	char file[128];
	int32_t f;

	sprint(file, "#p/%d/notepg", pid);
	f = open(file, OWRITE);
	if(f < 0){
		sprint(file, "/proc/%d/notepg", pid);
		f = open(file, OWRITE);
	}
	if(f < 0)
		werrstr("postnote: cannot open neither #p/%d/notepg nor /proc/%d/notepg", pid, pid);
	return f;
}

int
postnote(int group, int pid, const char *note)
{
	int f, r;

	switch(group) {
	case PNPROC:
		f = openprocnotes(pid);
		break;
	case PNGROUP:
		f = opengroupnotes(pid);
		break;
	default:
		werrstr("postnote: invalid group flag %d", group);
		return -1;
	}
	if(f < 0)
		return f;

	r = strlen(note);
	if(write(f, note, r) != r) {
		close(f);
		return -1;
	}
	close(f);
	return 0;
}
