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

typedef union {
	PosixSignalMask	signals;
	char		raw[sizeof(PosixSignalMask)];
} SigSetBuf;

extern SignalConf *__libposix_signals;
extern int *__restart_syscall;
extern int *__libposix_devsignal;

PosixSignalMask *__libposix_signal_mask;

static PendingSignalList *__libposix_pending_signals;
static PendingSignalList **__libposix_pending_signals_end;
static int __libposix_blocked_signals;

void
__libposix_reset_pending_signals(void)
{
	PendingSignalList *s;
	while((s = __libposix_pending_signals) != nil){
		__libposix_pending_signals = s->next;
		free(s);
	}
	__libposix_pending_signals_end = &__libposix_pending_signals;
	*__restart_syscall = 0;
	*__libposix_signal_mask = 0;
}

int
__libposix_signal_blocked(PosixSignalInfo *siginfo)
{
	PendingSignalList *l;

	if((*__libposix_signal_mask & SIGNAL_MASK(siginfo->si_signo)) == 0)
		return 0;

	/* Blocked signals are recorded at __libposix_pending_signals
	 * and will be delivered by sigprocmask when they will
	 * be unblocked
	 */
	l = malloc(sizeof(PendingSignalList));
	if(l == nil)
		return 1; /* signal discarded */

	memmove(&l->signal, siginfo, sizeof(PosixSignalInfo));
	l->next = nil;
	*__libposix_pending_signals_end = l;
	__libposix_pending_signals_end = &l->next;
	*__restart_syscall = 1;
	++__libposix_blocked_signals;
	return 1;
}

int
POSIX_sigaddset(int *errnop, PosixSignalMask *set, int signo)
{
	if(signo < 1 || signo > PosixNumberOfSignals){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	SIGNAL_RAW_ADD(*set, signo);
	return 0;
}

int
POSIX_sigdelset(int *errnop, PosixSignalMask *set, int signo)
{
	if(signo < 1 || signo > PosixNumberOfSignals){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	SIGNAL_RAW_DEL(*set, signo);
	return 0;
}

int
POSIX_sigismember(int *errnop, const PosixSignalMask *set, int signo)
{
	if(signo < 1 || signo > PosixNumberOfSignals){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	if(*set & SIGNAL_MASK(signo))
		return 1;
	return 0;
}

int
POSIX_sigfillset(int *errnop, PosixSignalMask *set)
{
	*set = ~0;
	return 0;
}

int
POSIX_sigemptyset(int *errnop, PosixSignalMask *set)
{
	*set = 0;
	return 0;
}

int
POSIX_sigpending(int *errnop, PosixSignalMask *set)
{
	PendingSignalList *p;
	PosixSignalMask tmp;
	if(set == nil){
		*errnop = __libposix_get_errno(PosixEFAULT);
		return -1;
	}

	tmp = __libposix_sighelper_cmd(PHGetPendingSignals, 0);

	/* include pending signals from outside the session */
	p = __libposix_pending_signals;
	while(p != nil){
		tmp |= SIGNAL_MASK(p->signal.si_signo);
		p = p->next;
	}

	*set = tmp;
	return 0;
}

long
__libposix_sighelper_wait(PosixSignalMask set, PosixSignalInfo *siginfo)
{
	return sys_pread(*__libposix_devsignal, siginfo, sizeof(PosixSignalInfo), set);
}

int
POSIX_sigtimedwait(int *errnop, const PosixSignalMask *set,
				PosixSignalInfo *info, const struct timespec *timeout)
{
	long wkp = 0, r;
	PendingSignalList *p, **end;
	PosixSignalInfo tmp;
	PosixError e = 0;
	int ms = -1, bs;

	if(set == nil){
		e = PosixEFAULT;
		goto FailWithError;
	}
	if(timeout != nil){
		if(timeout->tv_sec < 0 || timeout->tv_nsec < 0){
			e = PosixEINVAL;
			goto FailWithError;
		}
		ms = timeout->tv_sec * 1000;
		ms += timeout->tv_nsec / 1000000;
	}
	if(info == nil){
		memset(&tmp, 0, sizeof(PosixSignalInfo));
		info = &tmp;
	}

LookupPendingSignals:
	bs = __libposix_blocked_signals;
	end = &__libposix_pending_signals;
	for(p = *end; p != nil; p = *end){
		if((*set & SIGNAL_MASK(p->signal.si_signo)) != 0){
			memcpy(info, &p->signal, sizeof(PosixSignalInfo));
			*end = p->next;
			free(p);
			if(__libposix_pending_signals == nil)
				__libposix_pending_signals_end = &__libposix_pending_signals;
			goto Done;
		}
		end = &p->next;
	}

	if(bs != __libposix_blocked_signals)
		goto LookupPendingSignals;
	if(ms == 0){
		/* ms == 0 means that timeout has both fields to zero */
		if(__libposix_sighelper_cmd(PHGetPendingSignals, 0) == 0){
			e = PosixEAGAIN;
			goto FailWithError;
		}
	}

	if(ms > 0)
		wkp = sys_awake(ms);
	r = __libposix_sighelper_wait(*set, info);
	if(r < 0){
		if(ms > 0 && awakened(wkp)){
			/* timed out */
			e = PosixEAGAIN;
			goto FailWithError;
		}
		if(bs != __libposix_blocked_signals){
LookupPendingSignals2:
			bs = __libposix_blocked_signals;
			end = &__libposix_pending_signals;
			for(p = *end; p != nil; p = *end){
				if((*set & SIGNAL_MASK(p->signal.si_signo)) != 0){
					memcpy(info, &p->signal, sizeof(PosixSignalInfo));
					*end = p->next;
					free(p);
					if(__libposix_pending_signals == nil)
						__libposix_pending_signals_end = &__libposix_pending_signals;
					goto Done;
				}
				end = &p->next;
			}

			if(bs != __libposix_blocked_signals)
				goto LookupPendingSignals2;
		}
		e = PosixEINTR;
		goto FailWithError;
	}
	if(ms > 0)
		forgivewkp(wkp);

Done:
	return info->si_signo;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_sigprocmask(int *errnop, PosixSigProcMaskAction how, const PosixSignalMask *set, PosixSignalMask *oset)
{
	PendingSignalList *p, **e;
	PosixSignalMask new;
	PosixSignalMask old = *__libposix_signal_mask;
	int sigindex;
	if(set){
		new = *set;
		new &= ~((255UL<<56) | (PosixSIGKILL-1) | (PosixSIGSTOP-1));
		switch(how){
		case PosixSPMSetMask:
			*__libposix_signal_mask = new;
			break;
		case PosixSPMBlock:
			*__libposix_signal_mask |= new;
			break;
		case PosixSPMUnblock:
			*__libposix_signal_mask &= ~(new);
			break;
		default:
			*errnop = __libposix_get_errno(PosixEINVAL);
			return -1;
		}
		__libposix_sighelper_set(PHBlockSignals, *__libposix_signal_mask);
	}
	if(oset)
		*oset = old;
	new = *__libposix_signal_mask;
	if(old & (~new)){
		e = &__libposix_pending_signals;
		for(p = *e; p != nil; p = *e){
			sigindex = p->signal.si_signo - 1;
			if((new & (1ULL << sigindex)) == 0){
				__libposix_run_signal_handler(__libposix_signals + sigindex, &p->signal);
				*e = p->next;
				free(p);
			} else {
				e = &p->next;
			}
		}
	}

	return 0;
}
