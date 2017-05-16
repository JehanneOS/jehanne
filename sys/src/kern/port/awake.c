/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2015-2017 Giacomo Tesio <giacomo@tesio.it>
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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

typedef struct PendingWakeup PendingWakeup;
struct PendingWakeup
{
	short notified;
	uint64_t time;
	Proc *p;
	PendingWakeup *next;
};

/* alarms: linked list, sorted by wakeup time, protected by qlock(&l)
 * wakeupafter inserts new items, forgivewakeup remove them,
 * awakekproc consume the expired ones and clearwakeups remove those
 * survived to their process.
 */
static PendingWakeup *alarms;
static QLock l;

static Rendez	awaker;

int
canwakeup(Syscalls scall)
{
	if(scall == 0){
		/* on page fault */
		return 0;
	}
	switch(scall){
	default:
		panic("canwakeup: unknown scall %d\n", scall);
	case SysFstat:
	case SysFwstat:
	case SysOpen:
	case SysPread:
	case SysPwrite:
	case SysRendezvous:
	case SysSemacquire:
	case SysSemrelease:
	case SysAwait:
		return 1;
	case SysAwake:
	case SysBind:
	case SysClose:
	case SysCreate:
	case SysErrstr:
	case SysExec:
	case Sys_exits:
	case SysFauth:
	case SysFd2path:
	case SysFversion:
	case SysMount:
	case SysNoted:
	case SysNotify:
	case SysRemove:
	case SysRfork:
	case SysSeek:
	case SysUnmount:
	case SysAlarm:
		return 0;
	}
}

/*
 * Actually wakeup a process
 */
static void
wakeupProc(Proc *p, unsigned long t)
{
	Mpl pl;
	Rendez *r;
	Proc *d, **l;

	/* this loop is to avoid lock ordering problems. */
	for(;;){
		pl = splhi();
		lock(&p->rlock);
		r = p->r;

		/* waiting for a wakeup? */
		if(r == nil)
			break;	/* no */

		/* try for the second lock */
		if(canlock(&r->l)){
			if(p->state != Wakeme || r->p != p)
				panic("wakeupProc: state %d %d %d", r->p != p, p->r != r, p->state);
			p->r = nil;
			r->p = nil;
			ready(p);
			unlock(&r->l);
			break;
		}

		/* give other process time to get out of critical section and try again */
		unlock(&p->rlock);
		splx(pl);
		sched();
	}
	unlock(&p->rlock);
	splx(pl);

	p->pendingWakeup = t;
	if(p->state != Rendezvous)
		return;

	/* Try and pull out of a rendezvous */
	lock(&p->rgrp->l);
	if(p->state == Rendezvous) {
		p->rendval = ~0;
		l = &REND(p->rgrp, p->rendtag);
		for(d = *l; d; d = d->rendhash) {
			if(d == p) {
				*l = p->rendhash;
				break;
			}
			l = &d->rendhash;
		}
		ready(p);
	}
	unlock(&p->rgrp->l);
}

void
awakekproc(void* v)
{
	Proc *p;
	PendingWakeup *tail, *toAwake, **toAwakeEnd, *toDefer, **toDeferEnd;
	uint64_t now;

	for(;;){
		now = sys->ticks;
		toAwake = nil;
		toAwakeEnd = &toAwake;
		toDefer = nil;
		toDeferEnd = &toDefer;

		/* search for processes to wakeup */
		qlock(&l);
		tail = alarms;
		while(tail && tail->time <= now){
			if(tail->p->pendingWakeup > tail->p->lastWakeup
			&& tail->p->state >= Ready){
				*toDeferEnd = tail;
				toDeferEnd = &tail->next;
			} else if (!tail->notified && tail->p->notified){
				/* If an awake was requested outside of
				 * a note handler, it cannot expire
				 * while executing a note handler.
				 * On the other hand if an awake was
				 * requeted while executing a note handler,
				 * it's up to the note handler to
				 * forgive it if it's not needed anymore.
				 */
				*toDeferEnd = tail;
				toDeferEnd = &tail->next;
			} else {
				*toAwakeEnd = tail;
				toAwakeEnd = &tail->next;
				--tail->p->wakeups;
			}
			tail = tail->next;
		}
		if(toAwake != nil){
			*toAwakeEnd = nil;
			if(toDefer != nil){
				*toDeferEnd = tail;
				alarms = toDefer;
			} else {
				alarms = tail;
			}
		}
		qunlock(&l);

		/* wakeup sleeping processes */
		while(toAwake != nil){
			p = toAwake->p;
			if(p->lastWakeup < toAwake->time && p->state > Ready) {
				/* debugged processes will miss wakeups,
				 * but the alternatives seem even worse
				 */
				if(canqlock(&p->debug)){
					if(!waserror()){
						wakeupProc(p, toAwake->time);
						poperror();
					}
					qunlock(&p->debug);
				}
			}
			tail = toAwake->next;
			jehanne_free(toAwake);
			toAwake = tail;
		}

		sleep(&awaker, return0, 0);
	}
}

/*
 * called on pexit
 */
void
clearwakeups(Proc *p)
{
	PendingWakeup *w, **next, *freelist, **fend;

	freelist = nil;

	/* find all PendingWakeup* to free and remove them from alarms */
	qlock(&l);
	if(p->wakeups){
		/* note: qlock(&l) && p->wakeups > 0 => alarms != nil */
		next = &alarms;
		fend = &freelist;
clearnext:
		w = *next;
		while (w != nil && w->p == p) {
			/* found one to remove */
			*fend = w;			/* append to freelist */
			*next = w->next;	/* remove from alarms */
			fend = &w->next;	/* move fend to end of freelist */
			*fend = nil;		/* clean the end of freelist */
			--p->wakeups;
			w = *next;			/* next to analyze */
		}
		/* while exited => w == nil || w->p != p */
		if(p->wakeups){
			/* note: p->wakeups > 0 => w != nil
			 *       p->wakeups > 0 && w->p != p => w->next != nil
			 */
			next = &w->next;
			goto clearnext;
		}
	}
	qunlock(&l);

	/* free the found PendingWakeup* (out of the lock) */
	w = freelist;
	while(w != nil) {
		freelist = w->next;
		jehanne_free(w);
		w = freelist;
	}
}

/*
 * called every clock tick
 */
void
checkwakeups(void)
{
	PendingWakeup *p;
	uint64_t now;

	p = alarms;
	now = sys->ticks;

	if(p && p->time <= now)
		wakeup(&awaker);
}

static int64_t
wakeupafter(int64_t ms)
{
	PendingWakeup *w, *new, **last;
	int64_t when;

	when = ms2tk(ms) + sys->ticks + 2; /* +2 against round errors and cpu's clocks misalignment */
	new = jehanne_mallocz(sizeof(PendingWakeup), 1);
	if(new == nil)
		return 0;
	new->p = up;
	new->notified = up->notified;
	new->time = when;

	qlock(&l);
	last = &alarms;
	for(w = *last; w != nil && w->time <= when; w = w->next) {
		last = &w->next;
	}
	new->next = w;
	*last = new;
	++up->wakeups;
	qunlock(&l);

	return -when;
}

static int64_t
forgivewakeup(int64_t time)
{
	PendingWakeup *w, **last;

	if(up->lastWakeup >= time || up->pendingWakeup >= time)
		return 0;
	qlock(&l);
	if(alarms == nil || up->wakeups == 0){
		qunlock(&l);
		return 0;	// nothing to do
	}

	last = &alarms;
	for(w = *last; w != nil && w->time < time; w = w->next) {
		last = &w->next;
	}
	while(w != nil && w->time == time && w->p != up){
		last = &w->next;
		w = w->next;
	}
	if(w == nil || w->time > time || w->p != up){
		/* wakeup not found */
		qunlock(&l);
		return 0;
	}

	*last = w->next;
	--up->wakeups;
	qunlock(&l);

	jehanne_free(w);

	return -time;
}

int64_t
procawake(int64_t ms)
{
	if(ms == 0)
		return -up->lastWakeup; // nothing to do
	if(ms < 0)
		return forgivewakeup(-ms);
	return wakeupafter(ms);
}
