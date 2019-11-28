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
#include <lib9.h>

int verbose = 0;
int procNoteReceived;
int groupNoteReceived;

int
printFromProc(void *v, char *s)
{
	/* just not exit, please */
	if(strcmp(s, "proc") == 0){
		if(procNoteReceived)
			atnotify(printFromProc, 0);
		procNoteReceived++;
		if(verbose)
			fprint(2, "%d: noted: %s at %lld\n", getpid(), s, nsec());
		return 1;
	}
	return 0;
}

int
printFromGroup(void *v, char *s)
{
	/* just not exit, please */
	if(strcmp(s, "group") == 0){
		if(groupNoteReceived)
			atnotify(printFromGroup, 0);
		groupNoteReceived++;
		if(verbose)
			fprint(2, "%d: noted: %s at %lld\n", getpid(), s, nsec());
		return 1;
	}
	return 0;
}

int
fail(void *v, char *s)
{
	if(groupNoteReceived == 2 || procNoteReceived == 2){
		print("FAIL\n");
		exits("FAIL");
	}
	return 0;
}

void
main(void)
{
	int ppid;
	Waitmsg *w;
	
	if (!atnotify(printFromProc, 1) || !atnotify(printFromGroup, 1) || !atnotify(fail, 1)){
		fprint(2, "%r\n");
		exits("atnotify fails");
	}
	
	sys_rfork(RFNOTEG|RFNAMEG);

	if(sys_rfork(RFPROC) == 0){
		ppid = getppid(); // this comes from exec() syscall

		postnote(PNPROC, ppid, "proc");
		postnote(PNGROUP, getpid(), "group");
		
		sys_rfork(RFNOMNT);
		postnote(PNPROC, ppid, "proc");
		postnote(PNGROUP, getpid(), "group");
		
		sys_rfork(RFNOMNT|RFCNAMEG);
		if(postnote(PNPROC, ppid, "proc") != -1 || 
			postnote(PNGROUP, getpid(), "group") != -1){
			exits("FAIL");
		}

		exits(nil);
	} else {
		while((w = wait()) == nil)
			;
		if(w->msg[0]){
			exits("FAIL");
		}
		if(procNoteReceived < 2 || groupNoteReceived < 2){
			fprint(2, "FAIL: not enough notes.\n");
			exits("FAIL");
		}
		print("PASS\n");
		exits("PASS");
	}
}
