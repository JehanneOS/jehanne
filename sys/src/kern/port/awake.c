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

/* Requirements:
 * - each process can register several wakeups
 *   - wakeups registered outside a note handler will not happen
 *     in a note handler
 *   - wakeups registered by a note handler will be deleted on noted()
 *   - all pending wakeups of a process will be deleted on pexit()
 * - future wakeups can be deleted
 * - each wakeup will interrupt a blocking syscall
 */
struct AwakeAlarm	/* 24 byte */
{
	Proc*		p;
	AwakeAlarm*	next;
	unsigned char	notified;
	unsigned char	done;
	unsigned long	time	: 48;
};
typedef struct AlarmPool
{
	AwakeAlarm*	alarms;
	AwakeAlarm*	end;
	int		size;
	int		first;
	int		nfree;
	Lock		l;
} AlarmPool;
static AlarmPool awkpool;
#define alarm2wkp(a) ~((a)->time << 16 | (a) - awkpool.alarms)
#define wkp2alarm(a) (awkpool.alarms + (~(a) & 0xffff))

static AwakeAlarm *registry;
static Lock rl;


typedef enum ElapsedAlarmFate
{
	TryAgain,
	Forget,
	FreeAndForget
} ElapsedAlarmFate;
static AwakeAlarm *elapsed, **eEnd = &elapsed;
static Lock el;

static Rendez	producer;
static Rendez	consumer;

static unsigned long next_wakeup;

static const int awakeable_syscalls[] = {
	[SysAwait]	= 1,
	[SysAwake]	= 0,
	[SysBind]	= 0,
	[SysClose]	= 0,
	[SysCreate]	= 0,
	[SysErrstr]	= 0,
	[SysExec]	= 0,
	[Sys_exits]	= 0,
	[SysFauth]	= 0,
	[SysFd2path]	= 0,
	[SysFstat]	= 1,
	[SysFversion]	= 0,
	[SysFwstat]	= 1,
	[SysMount]	= 0,
	[SysNoted]	= 0,
	[SysNotify]	= 0,
	[SysOpen]	= 1,
	[SysPread]	= 1,
	[SysPwrite]	= 1,
	[SysRemove]	= 0,
	[SysRendezvous]	= 1,
	[SysRfork]	= 0,
	[SysSeek]	= 0,
	[SysSemacquire]	= 1,
	[SysSemrelease]	= 0,
	[SysUnmount]	= 0,
	[SysAlarm]	= 0,
};

#define DEBUG

#ifdef DEBUG

void
awake_detect_loop(AwakeAlarm *head)
{
	AwakeAlarm *tail;
	while(head != nil){
		if(!head->p)
			panic("awake: free alarm head: %#p, pc %#p", head, getcallerpc());
		if(head < awkpool.alarms || head > awkpool.end)
			panic("awake: not an alarm head: %#p, pc %#p", head, getcallerpc());
		tail = head->next;
		while(tail != nil){
			if(!tail->p)
				panic("awake: free alarm tail: %#p, pc %#p", tail, getcallerpc());
			if(tail < awkpool.alarms || tail > awkpool.end)
				panic("awake: not an alarm tail: %#p, pc %#p", tail, getcallerpc());
			if(head == tail)
				panic("awake: loop detected");
			else
				tail = tail->next;
		}
		head = head->next;
	}
}

AwakeAlarm*
awake_find_first_of(Proc* p, AwakeAlarm* head)
{
	AwakeAlarm* a = head;
	if(head == nil)
		return nil;
	while(a < awkpool.end){
		if(a->p == p)
			return a;
		++a;
	}
	return nil;
}

static int
awake_can_interrupt(Syscalls scall)
{
	if(scall == 0)
		panic("awake_can_interrupts on page fault");
	if(scall >= sizeof(awakeable_syscalls) - 1)
		panic("awake_can_interrupts: unknown syscall %d", scall);
	return awakeable_syscalls[scall];
}

#else
# define awake_detect_loop(h)
# define awake_can_interrupt(scall)	(awakeable_syscalls[scall])
//# undef assert
//# define assert(a)
#endif

static void
pool_init(void)
{
	awkpool.size = sys->nproc * (sys->nmach + 1);
	if(awkpool.size >= 1<<16)
		awkpool.size = 1<<16;	/* we have 16 bit in the wakeup token for the index */
	awkpool.alarms = malloc(sizeof(AwakeAlarm) * awkpool.size);
	awkpool.end = awkpool.alarms + awkpool.size;
	awkpool.nfree = awkpool.size;
	awkpool.first = 0;
}

static AwakeAlarm*
alarm_new(Proc *p)
{
	AwakeAlarm *a;

	lock(&awkpool.l);
	while(awkpool.nfree <= 2*p->nwakeups){
		unlock(&awkpool.l);
		resrcwait("wait-wkp", nil);
		lock(&awkpool.l);
	}
	/* here we know that nfree > 0... */
	if(awkpool.first == awkpool.size){
		/* but we don't now where the first free is */
		awkpool.first = -1;
		while(++awkpool.first < awkpool.size && awkpool.alarms[awkpool.first].p)
			;
	}
	assert(awkpool.first < awkpool.size);

	a = awkpool.alarms + awkpool.first;
	a->p = p;

	if(adec(&awkpool.nfree) > 0){
UpdateFirst:
		while(++awkpool.first < awkpool.size && awkpool.alarms[awkpool.first].p)
			;
		if(awkpool.first == awkpool.size){
			awkpool.first = -1;
			goto UpdateFirst;
		}
	} else {
		awkpool.first = awkpool.size;
	}
	unlock(&awkpool.l);

	ainc(&p->nwakeups);
	a->done = 0;
	a->notified = p->notified ? 1 : 0;
	a->time = 0;
	a->next = nil;

	return a;
}
static void
alarm_free(AwakeAlarm* a)
{
	Proc *p = a->p;
#ifdef DEBUG
	a->next = (void*)getcallerpc();
#endif
	a->p = nil;
	ainc(&awkpool.nfree);
	adec(&p->nwakeups);
}

void
awake_fell_asleep(Proc *p)
{
	Syscalls cs = p->cursyscall;
	if(cs != 0 && cs != SysAwake){
		/* awake_register might sleep() on alarm_new and we
		 * don't want this sleep to be interrupted.
		 */
		p->wakeups[p->notified].blockingsc = cs;
		p->wakeups[p->notified].fell_asleep = sys->ticks;
	}
}

int
awake_should_wake_up(Proc *p)
{
	AwakeAlarm *a = p->wakeups[p->notified].elapsed;
	Syscalls blockingsc = p->wakeups[p->notified].blockingsc;
	return a != nil
	    && blockingsc
	    && awake_can_interrupt(blockingsc)
	    ;
}

void
awake_awakened(Proc *p)
{
	AwakeAlarm *a;
	Syscalls blockingsc = p->wakeups[p->notified].blockingsc;
	if(blockingsc && awake_can_interrupt(blockingsc))
	if(a = xchgm(&p->wakeups[p->notified].elapsed, nil)){
		p->wakeups[p->notified].awakened = sys->ticks;
		p->wakeups[p->notified].last = alarm2wkp(a);
		p->wakeups[p->notified].blockingsc = 0;
	}
}

/* called from noted() to remove all pending alarms */
void
awake_gc_note(Proc *p)
{
	AwakeAlarm *a, **last;

	assert(p->notified != 0);

	lock(&rl);
	awake_detect_loop(registry);

	/* first pending alarm */
	a = xchgm(&p->wakeups[1].elapsed, nil);
	p->wakeups[1].blockingsc = 0;

	/* then clear the registry */
	last = &registry;
	for(a = *last; a != nil && p->wakeups[1].count > 0; a = *last) {
		if(!a->p)
			panic("awake_reset: free alarm in registry");
		if(a->p == p && a->notified){
			adec(&p->wakeups[a->notified].count);
			*last = a->next;
			alarm_free(a);
		} else {
			last = &a->next;
		}
	}
	awake_detect_loop(registry);
	if(registry)
		next_wakeup = registry->time;
	unlock(&rl);
}

/*
 * called from pexit() and sysexec() to clear all pending
 */
void
awake_gc_proc(Proc *p)
{
	AwakeAlarm *a, **last;

	/* clear all wakeups (process is exiting) */
	lock(&rl);
	awake_detect_loop(registry);

	/* first pending alarm */
	p->wakeups[0].elapsed = nil;
	p->wakeups[0].blockingsc = 0;
	p->wakeups[1].elapsed = nil;
	p->wakeups[1].blockingsc = 0;

	/* then clear the registry */
	last = &registry;
	for(a = *last; a != nil && (p->wakeups[0].count > 0 || p->wakeups[1].count > 0); a = *last) {
		if(!a->p)
			panic("awake_reset: free alarm in registry");
		if(a->p == p){
			adec(&p->wakeups[a->notified].count);
			*last = a->next;
			alarm_free(a);
		} else {
			last = &a->next;
		}
	}
	awake_detect_loop(registry);
	if(registry)
		next_wakeup = registry->time;
	unlock(&rl);

	assert(p->wakeups[0].count == 0);
	assert(p->wakeups[1].count == 0);

	while(p->nwakeups > 0)
		resrcwait(nil, nil);
}

static long
awake_register(long ms)
{
	AwakeAlarm *a, *new, **last;

	new = alarm_new(up);

	ilock(&rl);
	last = &registry;
	awake_detect_loop(registry);

	new->time = (sys->ticks + ms2tk(ms) + sys->nmach) & 0xffffffffffff;
	for(a = *last; a != nil && a->time <= new->time; a = *last) {
		if(!a->p)
			panic("awake_register: free alarm in registry");
		if(a->done){
			adec(&a->p->wakeups[a->notified].count);
			*last = a->next;
			alarm_free(a);
			continue;
		}
		if(a->time == new->time && a->p == up){
			/* avoid two alarms at the same tick for a process */
			++new->time;
		}
		last = &a->next;
	}
	*last = new;
	new->next = a;
	awake_detect_loop(registry);
	ainc(&up->wakeups[new->notified].count);
	assert(registry != nil);
	next_wakeup = registry->time;
	iunlock(&rl);

	return alarm2wkp(new);
}

static long
awake_remove(long request)
{
	AwakeAlarm *a;

	if(request >= up->wakeups[up->notified].last)
		return 0;	/* already free */

	a = wkp2alarm(request);
	if(a >= awkpool.end)
		return 0;	/* should we send a note to up? */

	if(a->p != up)
		return 0;	/* should we send a note to up? */

	if(a->time != (~request)>>16)
		return 0;	/* should we send a note to up? */

	lock(&rl);		/* sync with awake_timer */
	if(!CASV(&up->wakeups[up->notified].elapsed, a, nil)){
		a->done = 1;
	}
	unlock(&rl);

	return request;
}

long
sysawake(long request)
{
	if(request == 0)
		return up->wakeups[up->notified].last;
	if(request < 0)
		return awake_remove(request);
	return awake_register(request);
}

/*
 * called every clock tick
 */
void
awake_tick(unsigned long now)
{
	if(next_wakeup < now && (now&7) == 0)
		wakeup(&producer);
}

static void
try_wire_process(void)
{
	int i;
	/* wire to an online processor to reduce context switches
	 * but try to avoid boot processor as it runs awake_tick
	 */
	if(up->mach->machno > 0)
		procwired(up, up->mach->machno);
	else if(sys->nmach > 1){
		i = sys->nmach;
		while(up->wired != nil && i > 1)
			if(sys->machptr[--i]->online)
				procwired(up, i);
	}
}

void
awake_timer(void* v)
{
	long now;
	AwakeAlarm **next, *tmp, *toAwake, **toAwakeEnd;

	try_wire_process();

	/* initialize wakeups wkppool */
	pool_init();

CheckWakeups:
	next_wakeup = ~0;

	/* we fix time to preserve wakeup order */
	now = sys->ticks;

	toAwake = nil;
	toAwakeEnd = &toAwake;

	/* search for processes to wakeup */
	ilock(&rl);
	next = &registry;
	while((tmp = *next) != nil && tmp->time <= now){
		if(tmp->p == nil)
			panic("awake_timer: free alarm in registry");
		if(tmp->done){
			*next = tmp->next;
			adec(&tmp->p->wakeups[tmp->notified].count);
			alarm_free(tmp);
		} else {
			if(!CASV(&tmp->p->wakeups[tmp->notified].elapsed, nil, tmp)){
				/* each wakeup must have a chance */
				next = &tmp->next;
			} else {
				adec(&tmp->p->wakeups[tmp->notified].count);
				*toAwakeEnd = tmp;
				toAwakeEnd = &tmp->next;
				*next = tmp->next;
			}
		}
	}
	if(toAwake != nil)
		*toAwakeEnd = nil;

	awake_detect_loop(registry);
	iunlock(&rl);

	if(toAwake != nil){
		/* pass the elapsed wakeups to awake_ringer preserving order */
		lock(&el);
		*eEnd = toAwake;
		eEnd = toAwakeEnd;
		unlock(&el);
		wakeup(&consumer);
	}

	if(registry)
		next_wakeup = registry->time;

	sleep(&producer, return0, 0);
	goto CheckWakeups;
}

/* Try to wake up a process
 * Returns:
 * - 0 if the alarm must be preserved in the elapsed list
 * - 1 if the alarm must be removed from the elapsed list and freed
 */
static int
awake_dispatch(AwakeAlarm *a)
{
	int canfree;
	Rendez *r;
	Proc *p;
	Syscalls bs;

	p = a->p;
	if(p == nil)
		panic("awake_dispatch: free alarm in elapsed list");

	if(p < procalloc.arena || p > procalloc.arena + procalloc.nproc)
		panic("awake_dispatch: dirty alarm");
	if(!canlock(&p->rlock))
		return 0;

	/* sched() locks p->rlock before setting p->mach and p->state
	 */
	if(p->mach != nil && p->state <= Running){
		canfree = p->state < Ready;
		goto Done;
	}

	if(p->wakeups[a->notified].elapsed != a){
		/* cleared by awake_awakened */
		canfree = 1;
		goto Done;
	}

	canfree = 0;

	if(a->done){
		/* already signaled, we have to wait for awake_awakened */
		goto Done;
	}

	if(a->notified && !p->notified){
		/* this should never happen because p is not Running:
		 * either noted as been called previously (and thus awake_reset)
		 * or it has not
		 *
		 * so if this happens it's probably a but in awake_reset
		 */
		unlock(&p->rlock);
		panic("awake_dispatch: notified alarm for not notified process");
	}

	if(p->notepending && !p->notedeferred){
		/* notes take precedence */
		goto Done;
	}

	bs = p->wakeups[a->notified].blockingsc;
	if(bs == 0 || !awake_can_interrupt(bs)){
		/* wait for a chance */
		goto Done;
	}

	r = p->r;
	if(r != nil){
		if(canlock(&r->l)){
			if(p->state != Wakeme || r->p != p)
				panic("awake_dispatch: state %d %d %d", r->p != p, p->r != r, p->state);
			a->done = 1;
			p->r = nil;
			r->p = nil;
			ready(p);
			unlock(&r->l);
		}
	} else {
		if(p->state == Rendezvous || p->state == Queueing)
		if(proc_interrupt_finalize(p))
			a->done = 1;
	}

Done:
	unlock(&p->rlock);
	return canfree;
}

void
awake_ringer(void* v)
{
	AwakeAlarm *pending, **pLast, **next, *a;

	pending = nil;
	pLast = &pending;

CheckElapsed:
	lock(&el);
	*pLast = elapsed;
	elapsed = nil;
	eEnd = &elapsed;
	unlock(&el);

	pLast = &pending;
	next = pLast;
	while(*next != nil){
		a = *next;
		if(awake_dispatch(a)){
			*next = a->next;
			alarm_free(a);
		} else {
			*pLast = a;
			next = &a->next;
			pLast = next;
		}
	}

	if(pending == nil){
		assert(pLast == &pending);
		sleep(&consumer, return0, 0);
	} else if(elapsed == nil){
		tsleep(&up->sleep, return0, 0, 30);
	}
	goto CheckElapsed;
}
