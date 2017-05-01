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
#include <lib9.h>
#include <posix.h>
#include "internal.h"

extern char **environ;

WaitList **__libposix_wait_list;
static PosixSignalTrampoline __libposix_signal_trampoline;
static PosixExitStatusTranslator __libposix_exit_status_translator;
static int __libposix_wnohang;

#define __POSIX_SIGNAL_PREFIX_LEN (sizeof(__POSIX_SIGNAL_PREFIX)-1)

void
POSIX_exit(int code)
{
	char buf[64], *s;
	WaitList *wl, *c;

	/* free the wait list as the memory is shared */
	wl = *__libposix_wait_list;
	if(wl != nil){
		*__libposix_wait_list = nil;
		do
		{
			c = wl;
			wl = c->next;
			free(c);
		}
		while (wl != nil);
	}

	if(__libposix_exit_status_translator != nil
	&&(s = __libposix_exit_status_translator(code)) != nil){
		snprint(buf, sizeof(buf), "%s", s);
	} else {
		if(code == 0)
			exits(nil);
		snprint(buf, sizeof(buf), __POSIX_EXIT_PREFIX "%d", code);
	}
	exits(buf);
}

int
POSIX_execve(int *errnop, const char *name, char * const*argv, char * const*env)
{
	long ret;

	// see http://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html
	if(env == environ){
		/* just get a copy of the current environment */
		sys_rfork(RFENVG);
	} else {
		sys_rfork(RFCENVG);
		__libposix_setup_exec_environment(env);
	}

	ret = sys_exec(name, argv);
	switch(ret){
	case 0:
		return 0;
	case ~0:
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_execve);
		break;
	default:
		*errnop = ret;
		break;
	}
	return -1;
}

int
POSIX_wait(int *errnop, int *status)
{
	Waitmsg *w;
	WaitList *l;
	char *s;
	int ret = 0, pid;
	
	l = *__libposix_wait_list;
	if(l != nil){
		*__libposix_wait_list = l->next;
		if(status != nil)
			*status = l->status;
		pid = l->pid;
		free(l);
		return pid;
	}

	w = wait();
	if(w == nil){
		*errnop = __libposix_get_errno(PosixECHILD);
		return -1;
	}
	pid = w->pid;
	if(w->msg[0] != 0){
		s = strstr(w->msg, __POSIX_EXIT_PREFIX);
		if(s){
			s += (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1);
			ret = atoi(s);
		} else {
			/* TODO: setup configurable interpretations */
			ret = 127;
		}
	}
	if(status != nil)
		*status = ret << 8;
	free(w);
	return pid;
}

int
POSIX_waitpid(int *errnop, int reqpid, int *status, int options)
{
	Waitmsg *w;
	WaitList *c, **nl;
	char *s;
	int ret = 0, nohang = 0, pid;


	if(options & __libposix_wnohang){
		nohang = 1;
	}
//	else if(options != 0){
//		/* WARNING: WCONTINUED and WUNTRACED are not supported */
//		*errnop = __libposix_get_errno(PosixEINVAL);
//		return -1;
//	}

	switch(reqpid){
	case -1:
		if(nohang){
			*errnop = __libposix_get_errno(PosixEINVAL);
			return -1;
		}
		return POSIX_wait(errnop, status);
	case 0:
		/* not yet implemented; requires changes to Waitmsg */
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	default:
		if(reqpid < -1){
			/* not yet implemented; requires changes to Waitmsg */
			*errnop = __libposix_get_errno(PosixEINVAL);
			return -1;
		}
		break;
	}

	nl = __libposix_wait_list;
	c = *nl;
	while(c != nil){
		*nl = c->next;
		if(c->pid == reqpid){
			if(status != nil)
				*status = c->status;
			free(c);
			return reqpid;
		}
		c = *nl;
	}

WaitAgain:
	w = wait();
	if(w == nil){
		*errnop = __libposix_get_errno(PosixECHILD);
		return -1;
	}
	pid = w->pid;
	if(w->msg[0] != 0){
		s = strstr(w->msg, __POSIX_EXIT_PREFIX);
		if(s){
			s += (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1);
			ret = atoi(s);
		} else {
			/* TODO: setup configurable interpretations */
			ret = 127;
		}
	}
	if(pid == reqpid){
		if(status != nil)
			*status = ret << 8;
		return reqpid;
	}
	c = malloc(sizeof(WaitList));
	c->next = nil;
	c->pid = pid;
	c->status = ret << 8;
	*nl = c;
	if(!nohang)
		goto WaitAgain;
	*errnop = __libposix_get_errno(PosixECHILD);
	return -1;
}

static int
note_all_writable_processes(int *errnop, char *note)
{
	// TODO: loop over writable note files and post note.
	*errnop = __libposix_get_errno(PosixEPERM);
	return -1;
}

int
POSIX_kill(int *errnop, int pid, int sig)
{
	char msg[64], file[128];
	int mode;
	int ret;

	snprint(msg, sizeof(msg), __POSIX_SIGNAL_PREFIX "%d", sig);
	switch(pid){
	case 0:
		mode = PNGROUP;
		break;
	case -1:
		return note_all_writable_processes(errnop, msg);
	default:
		if(pid < 0){
			mode = PNGROUP;
			pid = -pid;
		} else {
			mode = PNPROC;
		}
	}

	snprint(file, sizeof(file), "/proc/%d/note", pid);
	if(access(file, AWRITE) != 0){
		if(access(file, AEXIST) == 0)
			*errnop = __libposix_get_errno(PosixEPERM);
		else
			*errnop = __libposix_get_errno(PosixESRCH);
		return -1;
	}
	ret = postnote(mode, pid, msg);
	if(ret != 0){
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_kill);
		return -1;
	}
	return 0;
}

int
POSIX_getpid(int *errnop)
{
	return getpid();
}

int
POSIX_getppid(int *errnop)
{
	return getppid();
}

int
POSIX_fork(int *errnop)
{
	int pid = fork();

	if(pid == 0){
		/* reset wait list for the child */
		*__libposix_wait_list = nil;
	}
	return pid;
}

static int
translate_jehanne_note(char *note)
{
	// TODO: implement
	return 0;
}

int
__libposix_note_handler(void *ureg, char *note)
{
	int sig;
	if(strncmp(note, __POSIX_SIGNAL_PREFIX, __POSIX_SIGNAL_PREFIX_LEN) != 0)
		return translate_jehanne_note(note); // TODO: should we translate common notes?
	sig = atoi(note+__POSIX_SIGNAL_PREFIX_LEN);
	return __libposix_signal_trampoline(sig);
}

int
libposix_translate_exit_status(PosixExitStatusTranslator translator)
{
	if(__libposix_initialized())
		return 0;
	if(translator == nil)
		return 0;
	__libposix_exit_status_translator = translator;
	return 1;
}

int
libposix_set_signal_trampoline(PosixSignalTrampoline trampoline)
{
	if(__libposix_initialized())
		return 0;
	if(trampoline == nil)
		return 0;
	__libposix_signal_trampoline = trampoline;
	return 1;
}

int
libposix_set_wait_options(int wcontinued, int wnohang, int wuntraced)
{
	if(wcontinued != 0)
		sysfatal("libposix: unsupported WCONTINUED");
	if(wuntraced != 0)
		sysfatal("libposix: unsupported WUNTRACED");
	__libposix_wnohang = wnohang;
	return 1;
}

void
__libposix_processes_check_conf(void)
{
	if(__libposix_signal_trampoline == nil)
		sysfatal("libposix: no signal trampoline");
	if(__libposix_wnohang == 0)
		sysfatal("libposix: WNOHANG is undefined");
	/* __libposix_exit_status_translator is optional */
}
