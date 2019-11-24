/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017-2019 Giacomo Tesio <giacomo@tesio.it>
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
ChildList **__libposix_child_list;

static PosixExitStatusTranslator __libposix_exit_status_translator;
static int __libposix_wnohang;

struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

struct rusage {
  	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	struct timeval ru_etime;	/* real elapsed time */
};

#define __POSIX_SIGNAL_PREFIX_LEN (sizeof(__POSIX_SIGNAL_PREFIX)-1)

static int
fork_without_sigchld(int *errnop)
{
	int pid = fork();
	if(pid == 0)
		__libposix_setup_new_process();
	return pid;
}

int (*__libposix_fork)(int *errnop) = fork_without_sigchld;

void
__libposix_free_wait_list(void)
{
	WaitList *tail, *w;

	/* free the wait list as the memory is NOT shared */
	tail = *__libposix_wait_list;
	while(w = tail){
		tail = tail->next;
		free(w);
	}
	*__libposix_wait_list = nil;
}

void
__libposix_free_child_list(void)
{
	ChildList *tail, *c;

	/* free the wait list as the memory is shared */
	tail = *__libposix_child_list;
	while(c = tail){
		tail = tail->next;
		free(c);
	}
	*__libposix_child_list = nil;
}

int
__libposix_is_child(int pid)
{
	ChildList *l;

	/* free the wait list as the memory is shared */
	l = *__libposix_child_list;
	while(l != nil){
		if(l->pid == pid)
			return 1;
		l = l->next;
	}
	return 0;
}

void
__libposix_forget_child(int pid)
{
	ChildList **l, *c;

	/* free the wait list as the memory is shared */
	l = __libposix_child_list;
	while(c = *l){
		if(c->pid == pid){
			*l = c->next;
			free(c);
			return;
		}
		l = &c->next;
	}
}

void
__libposix_setup_new_process(void)
{
	extern PosixSignalMask *__libposix_signal_mask;

	*__libposix_pid = getpid();

	/* reset wait list for the child */
	__libposix_free_wait_list();
	__libposix_free_child_list();

	/* clear pending signals; reload mask */
	__libposix_reset_pending_signals();
	__libposix_sighelper_open();
	*__libposix_signal_mask = (PosixSignalMask)__libposix_sighelper_cmd(PHGetProcMask, 0);
}

void
POSIX_exit(int code)
{
	char buf[64], *s;

	__libposix_free_wait_list();

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
POSIX_getrusage(int *errnop, PosixRUsages who, void *r_usagep)
{
	int32_t t[4];
	struct rusage *r_usage = r_usagep;

	times(&t[0]);
	switch(who){
	case PosixRUsageSelf:
		r_usage->ru_utime.tv_sec = t[0]/1000;
		r_usage->ru_utime.tv_sec = (t[0]%1000)*1000;
		r_usage->ru_stime.tv_sec = t[1]/1000;
		r_usage->ru_stime.tv_sec = (t[1]%1000)*1000;
		return 0;
	case PosixRUsageChildren:
		r_usage->ru_utime.tv_sec = t[2]/1000;
		r_usage->ru_utime.tv_sec = (t[2]%1000)*1000;
		r_usage->ru_stime.tv_sec = t[3]/1000;
		r_usage->ru_stime.tv_sec = (t[3]%1000)*1000;
		return 0;
	default:
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
}

int
POSIX_execve(int *errnop, const char *name, char * const*argv, char * const*env)
{
	// see http://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html
	if(env == environ){
		/* just get a copy of the current environment */
		rfork(RFENVG);
	} else {
		rfork(RFCENVG);
		__libposix_setup_exec_environment(env);
	}

	__libposix_free_wait_list();
	__libposix_close_on_exec();
	__libposix_sighelper_cmd(PHCallingExec, 0);

	exec(name, argv);
	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_execve);
	return -1;
}

int
POSIX_getpid(int *errnop)
{
	return *__libposix_pid;
}

int
POSIX_getppid(int *errnop)
{
	return getppid();
}

int
POSIX_fork(int *errnop)
{
	return __libposix_fork(errnop);
}

static int
__libposix_wait(int *errnop, int *status, long ms)
{
	Waitmsg *w;
	WaitList *l;
	char *s, err[ERRMAX];
	int ret = 0, sig = 0, pid;
	long wakeup = 0;
	
	l = *__libposix_wait_list;
	if(l != nil){
		*__libposix_wait_list = l->next;
		if(status != nil)
			*status = l->status;
		pid = l->pid;
		free(l);
		return pid;
	}

OnIgnoredSignalInterrupt:
	if(ms)
		wakeup = awake(ms);
	w = wait();
	if(w == nil){
		rerrstr(err, ERRMAX);
		if(strstr(err, "no living children") != nil){
			*errnop = __libposix_get_errno(PosixECHILD);
			return -1;
		}
		if(__libposix_restart_syscall()){
			if(wakeup)
				forgivewkp(wakeup);
			goto OnIgnoredSignalInterrupt;
		}
		if(wakeup){
			if(awakened(wakeup))
				return 0;
			forgivewkp(wakeup);
		}
		*errnop = __libposix_get_errno(PosixECHILD);
		return -1;
	}
	pid = w->pid;
	__libposix_forget_child(pid);
	if(w->msg[0] != 0){
		s = strstr(w->msg, __POSIX_EXIT_PREFIX);
		if(s){
			s += (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1);
			ret = atoi(s);
		} else {
			s = strstr(w->msg, __POSIX_EXIT_SIGNAL_PREFIX);
			if(s){
				s += (sizeof(__POSIX_EXIT_SIGNAL_PREFIX)/sizeof(char) - 1);
				sig = atoi(s);
			} else {
				/* TODO: setup configurable interpretations */
				ret = 127;
			}
		}
	}
	if(status != nil){
		if(sig == 0)
			*status = ret << 8;
		else
			*status = sig;
	}
	free(w);
	return pid;
}

int
POSIX_umask(int *errnop, int mask)
{
	return 0;
}

int
POSIX_wait(int *errnop, int *status)
{
	return __libposix_wait(errnop, status, 0);
}

int
POSIX_waitpid(int *errnop, int reqpid, int *status, int options)
{
	Waitmsg *w;
	WaitList *c, **nl;
	char *s, err[ERRMAX];
	int ret = 0, sig = 0, pid;
	long nohang = 0;


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
			return __libposix_wait(errnop, status, 100);
		}
		return __libposix_wait(errnop, status, 0);
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
		if(c->pid == reqpid){
			if(status != nil)
				*status = c->status;
			*nl = c->next;
			free(c);
			return reqpid;
		}
		nl = &c->next;
		c = *nl;
	}

WaitAgain:
OnIgnoredSignalInterrupt:
	if(nohang)
		nohang = awake(100);
	w = wait();
	if(w == nil){
		rerrstr(err, ERRMAX);
		if(strstr(err, "no living children") != nil){
			*errnop = __libposix_get_errno(PosixECHILD);
			return -1;
		}
		if(__libposix_restart_syscall()){
			if(nohang)
				forgivewkp(nohang);
			goto OnIgnoredSignalInterrupt;
		}
		if(nohang){
			if(awakened(nohang))
				return 0;
			forgivewkp(nohang);
		}
		*errnop = __libposix_get_errno(PosixECHILD);
		return -1;
	}
	if(nohang)
		forgivewkp(nohang);
	pid = w->pid;
	__libposix_forget_child(pid);
	if(w->msg[0] != 0){
		s = strstr(w->msg, __POSIX_EXIT_PREFIX);
		if(s){
			s += (sizeof(__POSIX_EXIT_PREFIX)/sizeof(char) - 1);
			ret = atoi(s);
		} else {
			s = strstr(w->msg, __POSIX_EXIT_SIGNAL_PREFIX);
			if(s){
				s += (sizeof(__POSIX_EXIT_SIGNAL_PREFIX)/sizeof(char) - 1);
				sig = atoi(s);
			} else {
				/* TODO: setup configurable interpretations */
				ret = 127;
			}
		}
	}
	if(pid == reqpid){
		if(status != nil){
			if(sig == 0)
				*status = ret << 8;
			else
				*status = sig;
		}
		return reqpid;
	}
	c = malloc(sizeof(WaitList));
	c->next = nil;
	c->pid = pid;
	if(sig == 0)
		c->status= ret << 8;
	else
		c->status = sig;
	*nl = c;
	if(!nohang)
		goto WaitAgain;
	*errnop = __libposix_get_errno(PosixECHILD);
	return -1;
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
	if(__libposix_wnohang == 0)
		sysfatal("libposix: WNOHANG is undefined");
	/* __libposix_exit_status_translator is optional */
}
