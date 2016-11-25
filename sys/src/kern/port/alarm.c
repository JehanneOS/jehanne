#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

static Alarms	alarms;
static Rendez	alarmr;

void
alarmkproc(void* _1)
{
	Proc *rp;
	uint64_t now;

	for(;;){
		now = sys->ticks;
		qlock(&alarms.ql);
		while((rp = alarms.head) && tickscmp(now, rp->alarm) >= 0){
			if(rp->alarm != 0L){
				if(canqlock(&rp->debug)){
					if(!waserror()){
						postnote(rp, 0, "alarm", NUser);
						poperror();
					}
					qunlock(&rp->debug);
					rp->alarm = 0L;
				}else
					break;
			}
			alarms.head = rp->palarm;
		}
		qunlock(&alarms.ql);

		sleep(&alarmr, return0, 0);
	}
}

/*
 *  called every clock tick
 */
void
checkalarms(void)
{
	Proc *p;
	uint64_t now;

	p = alarms.head;
	now = sys->ticks;

	if(p != nil && tickscmp(now, p->alarm) >= 0)
		wakeup(&alarmr);
}

uint64_t
procalarm(uint64_t time)
{
	Proc **l, *f;
	uint64_t when, old;

	if(up->alarm)
		old = tk2ms(up->alarm - sys->ticks);
	else
		old = 0;
	if(time == 0) {
		up->alarm = 0;
		return old;
	}
	when = ms2tk(time)+sys->ticks;
	if(when == 0)
		when = 1;

	qlock(&alarms.ql);
	l = &alarms.head;
	for(f = *l; f; f = f->palarm) {
		if(up == f){
			*l = f->palarm;
			break;
		}
		l = &f->palarm;
	}

	up->palarm = 0;
	if(alarms.head) {
		l = &alarms.head;
		for(f = *l; f; f = f->palarm) {
			if(tickscmp(f->alarm, when) >= 0) {
				up->palarm = f;
				*l = up;
				goto done;
			}
			l = &f->palarm;
		}
		*l = up;
	}
	else
		alarms.head = up;
done:
	up->alarm = when;
	qunlock(&alarms.ql);

	return old;
}
