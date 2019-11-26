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

/* This is an helper for programs using libposix.
 *
 * It simulates the kernel management of signal related stuffs, like
 * - signal delivery: it translates signals to notes and send them
 * - signal masks: it discards ignored signals and defer blocked ones
 * - process groups
 * 
 * It represent a single login session.
 * It starts a new program in a new namespace providing /dev/posix/,
 * to serve applications based on libposix.
 * It translate notes received into signals for the foreground process
 * group.
 */

#include <u.h>
#include <lib9.h>
#include <envvars.h>
#include <9P2000.h>
#include <posix.h>
#include "internal.h"


extern int *__libposix_devsignal;

/* Type defs */
typedef struct Process Process;
typedef enum ProcessFlags ProcessFlags;
typedef struct ProcessGroup ProcessGroup;
typedef struct Signal Signal;
typedef struct SignalList SignalList;
typedef struct Waiter Waiter;
typedef struct Fid Fid;

/* Data structures */
enum ProcessFlags
{
	SessionLeader	= 1<<0,
	GroupLeader	= 1<<1,
	NannyProcess	= 1<<2,	/* waits for a child and produce SIGCHLD */

	CalledExec	= 1<<3,	/* do not free on close */
	Detached	= 1<<4,
};
struct ProcessInfos	/* for actual processes */
{
	PosixSignalMask	waited;
	SignalList*	signals;
};
struct NannyInfos	/* for SIGCHLD generators */
{
	int		ppid;
	int		cpid;
	Process*	child;
};
struct Process
{
	int		pid;
	int		children;
	int		noteid;
	ProcessFlags	flags;
	Process*	parent;
	ProcessGroup*	group;
	PosixSignalMask	blocked;
	PosixSignalMask	ignored;
	union{
		struct ProcessInfos;
		struct NannyInfos;	/* if flags & NannyProcess */
	};
	Process*	next;
};
struct Waiter
{
	uint16_t	tag;
	Fid*		owner;
	Signal*		signal;
	Waiter*		next;
};

struct ProcessGroup
{
	int		pgid;
	int		noteid;
	int		processes;
	ProcessGroup*	next;
};

struct Fid
{
	Qid qid;
	int16_t opened;		/* -1 when not open */
	int fd;
	Process* process;	/* if any */
	Fid *next;
};

/* Global variables */
char notebuf[ERRMAX];
int fspid;	// filesystem id
int sid;	// session leader id
int debugging = -1;
char *debug_prefix;
int debug_prefix_len;
char *user;
char *data;
extern void (*__assert)(char*);

ProcessGroup* foreground;
ProcessGroup* groups;
Process* processes;
Process* session_leader;
Waiter* waiting_process;


/* Utilities (DEBUG, devproc and so on) */
#define DEBUG(...) if (debugging != -1)debug(__VA_ARGS__)
static void
debug(const char *fmt, ...)
{
	va_list arg;

	if (debug_prefix == nil){
		debug_prefix = smprint("posix.%s.%d[%d]: ", user, sid, fspid);
		debug_prefix_len = strlen(debug_prefix);
	}
	jehanne_write(debugging, debug_prefix, debug_prefix_len);
	va_start(arg, fmt);
	vfprint(debugging, fmt, arg);
	va_end(arg);
}
static void
usage(void)
{
	fprint(2, "usage: %s [-d dbgfile] [-p pid | cmd [args...]]\n", argv0);
	exits("usage");
}

/* Signals */
struct Signal
{
	int		refs;
	PosixSignalInfo info;
	char		note[];
};
struct SignalList
{
	Signal*	signal;
	SignalList*	next;
};
static Signal*
signal_create(PosixSignalInfo *siginfo)
{
	int i;
	Signal *signal;

	__libposix_signal_to_note(siginfo, notebuf, sizeof(notebuf));

	i = strlen(notebuf);
	signal = malloc(sizeof(Signal)+i+1);
	signal->refs = 1;
	memmove(&signal->info, siginfo, sizeof(PosixSignalInfo));
	memmove(signal->note, notebuf, i);
	signal->note[i] = '\0';

	return signal;
}
static void
signal_free(Signal* signal)
{
	assert(signal != nil && signal->refs > 0);
	if(--signal->refs == 0)
		free(signal);
}

/* Interface to devproc */
static int
proc_noteid(int pid)
{
	long n;
	char buf[32];
	sprint(buf, "/proc/%d/noteid", pid);
	n = sys_remove(buf);
	DEBUG("proc_noteid(%d) = %lld\n", pid, n);
	if(n == -1)
		return -1;
	return (int)n;
}
static int
proc_set_noteid(int pid, int noteid)
{
	int n, f;
	char buf[32];

	assert(pid != 0);
	assert(noteid != 0);

	sprint(buf, "/proc/%d/noteid", pid);
	f = sys_open(buf, OWRITE);
	if(f < 0)
		return 0;
	n = sprint(buf, "%d", noteid);
	n = jehanne_write(f, buf, n);
	sys_close(f);
	if(n < 0)
		return 0;
	return 1;
}
static int
proc_ppid(int pid)
{
	long n;
	char buf[32];
	sprint(buf, "/proc/%d/ppid", pid);
	n = sys_remove(buf);
	DEBUG("proc_ppid(%d) = %lld\n", pid, n);
	if(n == -1)
		return -1;
	return (int)n;
}
static int
proc_dispatch_signal(int target, Signal *signal)
{
	DEBUG("proc_dispatch_signal(%d, '%s')\n", target, signal->note);
	if(postnote(PNPROC, target, signal->note) < 0)
		return -1;
	if(signal->info.si_signo == PosixSIGCONT)
		__libposix_send_control_msg(target, "start");
	return 1;
}
static int
proc_convert_signal(int target, Signal *signal)
{
	char *note;
	DEBUG("proc_convert_signal(%d, '%s')\n", target, signal->note);
	switch(signal->info.si_signo){
	case PosixSIGCONT:
		__libposix_send_control_msg(target, "start");
		return 1;
	case PosixSIGTSTP:
	case PosixSIGTTIN:
	case PosixSIGTTOU:
	case PosixSIGSTOP:
		__libposix_send_control_msg(target, "stop");
		return 1;
	case PosixSIGCHLD:
		/* simply don't send notes that are not expected */
		return 1;
	case PosixSIGHUP:
		note = "hangup";
		break;
	case PosixSIGALRM:
		note = "alarm";
		break;
	case PosixSIGINT:
		note = "interrupt";
		break;
	default:
		note = signal->note;
		break;
	}
	if(postnote(PNPROC, target, note) < 0)
		return -1;
	return 1;
}


/* ProcessGroups */
static int process_handle_signal(Process *target, Signal *signal);
static void process_free(Process *target);

static void
group_remove(ProcessGroup* group, Process* p)
{
	ProcessGroup **e, *g;
	assert(group == p->group);
	p->group = nil;
	if(--group->processes == 0){
		e = &groups;
		while(g = *e){
			if(group == g){
				*e = group->next;
				if(foreground == group)
					foreground = nil;
				free(group);
				return;
			}
			e = &g->next;
		}
		DEBUG("group_remove(%d, %d) not found\n", group->pgid, p->pid);
		sysfatal("group_remove: group %d not found", group->pgid);
	}
	DEBUG("group_remove(%d, %d): done\n", group->pgid, p->pid);
}
static int
group_add(ProcessGroup* group, Process* p)
{
	assert(group != p->group);
	if(!proc_set_noteid(p->pid, group->noteid)){
		DEBUG("group_add(%d, %d): cannot set noteid %d\n", group->pgid, p->pid, group->noteid);
		return 0;
	}
	group_remove(p->group, p);
	p->group = group;
	p->noteid = group->noteid;
	++group->processes;
	DEBUG("group_add(%d, %d): done\n", group->pgid, p->pid);
	return 1;
}
static ProcessGroup*
group_create(Process *leader)
{
	ProcessGroup* g;
	assert(leader != nil);

	if(leader->flags & SessionLeader)
		return nil;	/* operation not permitted */
	if(leader->group && leader->group->pgid == leader->pid)
		return leader->group;	/* nothing to do */

	g = mallocz(sizeof(ProcessGroup), 1);
	if(g == nil)
		return nil;

	DEBUG("group_create(%d)\n", leader->pid);
	leader->flags |= GroupLeader;
	g->pgid = leader->pid;
	g->noteid = proc_noteid(leader->pid);
	g->processes = 1;
	if(leader->group)
		group_remove(leader->group, leader);
	leader->group = g;
	if((leader->flags&NannyProcess) && leader->child != nil){
		group_add(g, leader->child);
	}
	g->next = groups;
	groups = g;
	DEBUG("group_create(%d): done\n", leader->pid);
	return g;
}
static void
group_handle_signal(ProcessGroup *group, Signal *signal)
{
	Process *p, *tmp, **e;
	assert(group != nil && signal != nil);
	DEBUG("group_handle_signal(%d, %d): dispatching\n", group->pgid, signal->info.si_signo);
	e = &processes;
	while(p = *e){
		if(p->group == group && !(p->flags & NannyProcess))
		if(process_handle_signal(p, signal) == -1 
		&&((p->flags & CalledExec)||(p->flags & Detached))){
			/* the signal dispatching (aka postnote) failed
			 * and the process called exec or setsid.
			 * Let assume that the process exited.
			 */
			tmp = p->next;
			DEBUG("group_handle_signal(%d, %d): removing (probably) dead process %d\n", group->pgid, signal->info.si_signo, p->pid);
			process_free(p);
			*e = tmp;
			continue;
		}
		e = &p->next;
	}
	DEBUG("group_handle_signal(%d, %d): done\n", group->pgid, signal->info.si_signo);
}
static int
group_foreground(Process *reqprocess, int pgid)
{
	static PosixSignalInfo siginfo;
	ProcessGroup *g;
	Signal *s;

	if(reqprocess->group == foreground
	||(reqprocess->blocked&SIGNAL_MASK(PosixSIGTTOU))
	||(reqprocess->ignored&SIGNAL_MASK(PosixSIGTTOU))){
		g = groups;
		while(g != nil && g->pgid != pgid)
			g = g->next;
		if(g == nil)
			return 0; /* group not found */
		foreground = g;
		return 1;
	}
	siginfo.si_signo = PosixSIGTTOU;
	s = signal_create(&siginfo);
	group_handle_signal(reqprocess->group, s);
	signal_free(s);
	return 0;
}
static ProcessGroup*
group_find(int gid)
{
	ProcessGroup* g;

	g = groups;
	while(g && g->pgid != gid)
		g = g->next;

	if(g && g->pgid == gid)
		return g;

	return nil;
}

/* Processes */
static Process*
process_find(int pid)
{
	Process* p;

	p = processes;
	while(p && p->pid < pid)
		p = p->next;

	if(p && p->pid == pid)
		return p;

	return nil;
}
static Process*
process_create(int pid)
{
	int ppid, tmp;
	Process *n, *p, *t, **e;

	n = mallocz(sizeof(Process), 1);
	if(n == nil)
		return nil;
	n->pid = pid;
	ppid = proc_ppid(pid);
	e = &processes;
	while((p = *e) && p->pid < pid){
		if(p->pid == ppid){
			n->parent = p;
			n->group = p->group;
			++n->group->processes;
		}
		e = &p->next;
	}
	if(p != nil && p->pid == pid){
		free(n);
		return nil;
	}
	if(n->parent == nil && session_leader != nil){
		tmp = ppid;
		while(tmp > sid){
			tmp = proc_ppid(tmp);
			t = process_find(tmp);
			if(t != nil){
				n->blocked = t->blocked;
				n->group = t->group;
				++n->group->processes;
				break;
			}
		}
	} else if(session_leader != nil){
		/* libposix must ensure that the creation of a process
		 * here occurs before the exit of the parent process
		 * (so fork must be wait for the child to speak with us)
		 */
		++n->parent->children;
		if(n->parent->flags & NannyProcess){
			assert(n->child == nil); /* one nanny for one child */
			n->parent->cpid = pid;
			n->parent->child = n;
		}
		n->blocked = n->parent->blocked;
		n->ignored = n->parent->ignored;
		++n->group->processes;
	} else {
		session_leader = n;
		foreground = group_create(session_leader);
		session_leader->flags |= SessionLeader;
	}
	n->noteid = proc_noteid(pid);
	n->next = p;
	*e = n;
	if(n->flags & SessionLeader){
		DEBUG("process_create(%d): session leader\n", pid);
	} else if(n->parent == nil){
		DEBUG("process_create(%d): ppid %d (NOT POSIX), gid %d\n", pid, ppid, n->group ? n->group->pgid : -1);
	} else {
		DEBUG("process_create(%d): ppid %d, gid %d\n", pid, n->parent->pid, n->group->pgid);
	}
	return n;
}
static Process*
process_create_nanny(int pid)
{
	Process *p = process_create(pid);
	if(p == nil)
		return nil;
	assert(p->parent != nil);
	p->flags |= NannyProcess;
	p->ppid = p->parent->pid;
	return p;
}
static void
process_free_nannies(Process *parent, Process **list)
{
	Process *p;
	char buf[256];
	int fd;

	while(parent->children > 0 && (p = *list)){
		if(p->parent == parent && (p->flags & NannyProcess)){
			*list = p->next;
			DEBUG("process_free_nannies(%d): killing pid %d\n", parent->pid, p->pid);
			snprint(buf, sizeof(buf), "/proc/%d/ctl", p->pid);
			fd = sys_open(buf, OWRITE);
			if(fd < 0){
				DEBUG("process_free_nannies(%d): cannot open '%s': %r\n", parent, buf);
			} else {
				jehanne_write(fd, "kill", 5);
			}
			sys_close(fd);
			if(p->child)
				p->child->parent = nil;
			free(p);
			--parent->children;
		} else {
			list = &p->next;
		}
	}
}
static void
process_free(Process *target)
{
	Process *p, *c, **e;
	Signal *s;
	PosixSignalInfo siginfo;

	if(target == session_leader){
		if(foreground != nil){
			memset(&siginfo, 0, sizeof(PosixSignalInfo));
			siginfo.si_signo = PosixSIGHUP;
			s = signal_create(&siginfo);
			group_handle_signal(foreground, s);
			signal_free(s);
		}
		session_leader = nil;
	}
	e = &processes;
	while(p = *e){
		if(p == target){
			*e = p->next;
			DEBUG("process_free(%d): gid %d\n", p->pid, p->group->pgid);
			if(p->children > 0 && !(p->flags&NannyProcess)){
				/* remove all sigchild proxies */
				process_free_nannies(p, e);
			}
			if(p->children > 0){
				c = *e;
				while(c && p->children > 0){
					if(c->parent == p){
						c->parent = nil;
						--p->children;
					}
					c = c->next;
				}
				assert(p->children == 0);
			}
			if(p->parent){
				--p->parent->children;
				if(p->parent->flags&NannyProcess)
					p->parent->child = nil;
			}
			free(p);
			return;
		}
		e = &p->next;
	}
	DEBUG("process_free(%d): not found!\n", target->pid);
}
static int
process_handle_signal(Process *target, Signal *signal)
{
	int tmp;
	Waiter *wp;
	SignalList *l;

	assert(target != nil && signal != nil);

	if(target->flags & NannyProcess){
		/* target is a proxy */
		if(signal->info.si_pid == target->cpid){
			/* this is a message from the child, to its parent */
			if(target->parent){
				signal->info.si_pid = target->pid;
				tmp = process_handle_signal(target->parent, signal);
				signal->info.si_pid = target->cpid;
				return tmp;
			}
			return 1;
		}
		if(signal->info.si_pid == target->ppid){
			/* this is a message from the parent, to its child */
			if(target->child){
				signal->info.si_pid = target->pid;
				tmp = process_handle_signal(target->child, signal);
				signal->info.si_pid = target->ppid;
				return tmp;
			}
			return 1;
		}
		/* this message is neither from the parent nor from the child
		 * but since only the parent can give away target pid
		 * as its own child pid, we assume the signal is for
		 * the child.
		 */
		if(target->child)
			return process_handle_signal(target->child, signal);
	}

	PosixSignalMask sigflag = SIGNAL_MASK(signal->info.si_signo);
	if(target->ignored & sigflag)
		return 1;
	if(target->waited & sigflag){
		/* the process is waiting this signal reading */
		wp = waiting_process;
		while(wp && wp->owner->process != target)
			wp = wp->next;
		assert(wp != nil && wp->owner->process == target);
		wp->signal = signal;
		++signal->refs;
		return 0;
	}
	if(target->blocked & sigflag){
		l = malloc(sizeof(SignalList));
		if(nil == l)
			return 1;
		l->signal = signal;
		l->next = target->signals;
		target->signals = l;
		++signal->refs;
		return 0;
	}
	if(target->flags & CalledExec)
		return proc_convert_signal(target->pid, signal);
	return proc_dispatch_signal(target->pid, signal);
}
void
process_block_signals(Process *p, PosixSignalMask signals)
{
	SignalList *s, **es;
	PosixSignalMask old;

	assert(p != nil);

	old = p->blocked;
	if(old == signals){
		DEBUG("process_block_signals(%d, %ullb): nothing to do\n", p->pid, signals);
		return;
	}
	p->blocked = signals;

	es = &p->signals;
	while(s = *es){
		if((signals & SIGNAL_MASK(s->signal->info.si_signo)) == 0){
			*es = s->next;
			DEBUG("process_block_signals(%d, %ullb): dispatching '%s'\n", p->pid, signals, s->signal->note);
			process_handle_signal(p, s->signal);
			signal_free(s->signal);
			free(s);
			continue;
		}
		es = &s->next;
	}
	DEBUG("process_block_signals(%d, %ullb): done\n", p->pid, signals);
}
void
process_enable_signal(Process *p, PosixSignalMask signalmask)
{
	PosixSignalMask old;
	
	assert(p != nil);

	old = p->ignored;
	if(old & signalmask){
		p->ignored &= ~signalmask;
		DEBUG("process_enable_signal(%d, %ullb): done\n", p->pid, signalmask);
	} else {
		DEBUG("process_enable_signal(%d, %ullb): nothing to do\n", p->pid, signalmask);
	}
}
void
process_ignore_signal(Process *p, PosixSignalMask signalmask)
{
	SignalList *s, **es;
	PosixSignalMask old;

	assert(p != nil);

	old = p->ignored;
	if(old & signalmask){
		DEBUG("ignore_signals(%d, %ullb): nothing to do\n", p->pid, signalmask);
		return;
	}
	p->ignored |= signalmask;

	es = &p->signals;
	while(s = *es){
		if(signalmask & SIGNAL_MASK(s->signal->info.si_signo)){
			*es = s->next;
			DEBUG("ignore_signals(%d, %ullb): discarded %d\n", p->pid, signalmask, s->signal->info.si_signo);
			signal_free(s->signal);
			free(s);
			continue;
		}
		es = &s->next;
	}
	DEBUG("ignore_signals(%d, %ullb): done\n", p->pid, signalmask);
}
static Signal*
process_find_pending_signal(Process *p, PosixSignalMask mask)
{
	SignalList *s, **e;
	Signal *found;
	assert(p != nil);

	e = &p->signals;
	while(s = *e){
		if(SIGNAL_MASK(s->signal->info.si_signo) & mask){
			*e = s->next;
			found = s->signal;
			free(s);
			return found;
		}
	}

	return nil;
}
static PosixSignalMask
process_pending_signals(Process *p)
{
	PosixSignalMask pending = 0;
	SignalList *s;

	assert(p != nil);
	s = p->signals;
	while(s){
		pending |= SIGNAL_MASK(s->signal->info.si_signo);
		s = s->next;
	}
	
	return pending;
}

/* File System */
typedef union
{
	long			value;
	PosixHelperRequest	request;
} LongConverter;
typedef union
{
	int	value;
	char	bytes[sizeof(int)];
} IntConverter;
enum
{
	Maxfdata	= 8192,
	Miniosize	= IOHDRSZ+sizeof(PosixSignalInfo),
	Maxiosize	= IOHDRSZ+Maxfdata,
};

typedef enum
{
	Initializing,
	Mounted,
	Unmounted,		/* fsserve() loop while status < Unmounted */
} Status;
Status status;

enum {
	Qroot,
	Qposix,
	Nqid,
	Qcontrol,
	Qsignals,
	Qnanny,
	Nhqid,
};
static struct Qtab {
	char *name;
	int mode;
	int type;
	int length;
} qtab[Nhqid] = {
	"/",
		DMDIR|0555,
		QTDIR,
		0,

	"posix",	/* create files on fork */
		DMDIR|0755,
		QTDIR,
		0,

	"",
		0,
		0,
		0,

	"control",
		0222,	/* jehanne_write to send signals */
		0,
		0,

	"signals",	/* read/jehanne_write to control signal management */
		0666,
		0,
		0,

	"nanny",	/* jehanne_write to send SIGCHLD */
		0222,
		0,
		0,
};



/* linked list of known fids
 *
 * NOTE: we don't free() Fids, because there's no appropriate point
 *	in 9P2000 to do that, except the Tclunk of the attach fid,
 *	that in our case corresponds to shutdown
 *	(the kernel is our single client, we are doomed to trust it)
 */
#define ISCLOSED(f) (f != nil && f->opened == -1)

static Fid *fids;
static Fid **ftail;
static Fid *external;	/* attach fid of the sys_mount() */
static Fid *control_fid;


static Fid*
fid_create(int fd, Qid qid)
{
	Fid *fid;

	fid = (Fid*)malloc(sizeof(Fid));
	if(fid){
		fid->fd = fd;
		fid->qid = qid;
		fid->opened = -1;
		fid->process = nil;
		fid->next = nil;
		*ftail = fid;
		ftail = &fid->next;
	}
	return fid;
}
static Fid*
fid_find(int fd)
{
	Fid *fid;

	fid = fids;
	while(fid != nil && fid->fd != fd)
		fid = fid->next;
	return fid;
}

/* 9p message handlers */
static int
fillstat(uint64_t path, Dir *d)
{
	struct Qtab *t;

	memset(d, 0, sizeof(Dir));
	d->uid = user;
	d->gid = user;
	d->muid = user;
	d->qid = (Qid){path, 0, 0};
	d->atime = time(0);
	t = qtab + path;
	d->name = t->name;
	d->qid.type = t->type;
	d->mode = t->mode;
	d->length = t->length;
	return 1;
}
static int
rootread(Fid *fid, uint8_t *buf, long off, int cnt, int blen)
{
	int m, n;
	long i, pos;
	Dir d;

	n = 0;
	pos = 0;
	for (i = 1; i < Nqid; i++){
		fillstat(i, &d);
		m = convD2M(&d, &buf[n], blen-n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				break;
			n += m;
			cnt -= m;
		}
		pos += m;
	}
	return n;
}
static int
fs_error(Fcall *rep, char *err)
{
	DEBUG("fs_error %#p: %s\n", __builtin_return_address(0), err);
	rep->type = Rerror;
	rep->ename = err;
	return 1;
}
#define fs_eperm(req, rep) fs_error(rep, "permission denied")
#define fs_enotfound(req, rep) fs_error(rep, "file does not exist")
static int
fs_attach(Fcall *req, Fcall *rep)
{
	char *spec;
	Fid *f;

	spec = req->aname;
	if(spec && spec[0]){
		return fs_error(rep, "bad attach specifier");
	}
	f = fid_find(req->fid);
	if(f == nil)
		f = fid_create(req->fid, (Qid){Qroot, 0, QTDIR});
	if(f == nil){
		return fs_error(rep, "out of memory");
	}

	external = f;
	status = Mounted;

	rep->type = Rattach;
	rep->qid = f->qid;

	return 1;
}
static int
fs_auth(Fcall *req, Fcall *rep)
{
	return fs_error(rep, "authentication not required");
}
static int
fs_version(Fcall *req, Fcall *rep)
{
	if(req->msize < Miniosize){
		return fs_error(rep, "message size too small");
	}
	rep->type = Rversion;
	rep->msize = req->msize;
	if(*req->version == 0 || strncmp(req->version, "9P2000", 6) == 0)
		rep->version = "9P2000";
	else
		rep->version = "unknown";
	return 1;
}
static int
fs_flush(Fcall *req, Fcall *rep)
{
	Fid *f;
	Waiter **e, *s;

	e = &waiting_process;
	while(s = *e){
		if(s->tag == req->oldtag){
			*e = s->next;
			f = s->owner;
			f->process->waited = 0;
			if(s->signal != nil){
				process_handle_signal(f->process, s->signal);
				signal_free(s->signal);
			}
			free(s);
			break;
		}
		e = &s->next;
	}
	rep->type = Rflush;
	return 1;
	
}
static int
fs_walk(Fcall *req, Fcall *rep)
{
	Fid *f, *n;
	Qid q;

	f = fid_find(req->fid);
	if(f == nil)
		return fs_error(rep, "bad fid");
	if(f->opened != -1)
		return fs_error(rep, "fid in use");
	if(req->nwname > 0 && f->qid.type != QTDIR)
		goto WalkInNonDirectory;

	if(req->fid == req->newfid){
		n = f;
	} else {
		n = fid_find(req->newfid);
		if(n == nil)
			n = fid_create(req->newfid, q);
		else if(n->opened != -1)
			return fs_error(rep, "newfid already in use");
		if(n == nil)
			return fs_error(rep, "out of memory");
	}
	if(req->nwname == 0){
		n->qid = f->qid;
		rep->type = Rwalk;
		rep->nwqid = 0;
		return 1;
	}

	// TODO handle '..'
	switch(f->qid.path){
	case Qroot:
		switch(req->nwname){
		case 1:
			if(strcmp(qtab[Qposix].name, req->wname[0]) != 0)
				goto FileNotExists;
			rep->wqid[0] = (Qid){Qposix, 0, QTDIR};
			break;
		case 2:
			if(strcmp(qtab[Qposix].name, req->wname[0]) != 0)
				goto FileNotExists;
			if(control_fid != nil 
			|| strcmp(qtab[Qcontrol].name, req->wname[1]) != 0)
				goto FileNotExists;
			rep->wqid[0] = (Qid){Qposix, 0, QTDIR};
			rep->wqid[1] = (Qid){Qcontrol, 0, 0};
			break;
		default:
			goto WalkInNonDirectory;
		}
		break;
	case Qposix:
		if(req->nwname > 1)
			goto WalkInNonDirectory;
		if(strcmp("..", req->wname[0]) == 0){
			rep->wqid[0] = (Qid){Qroot, 0, QTDIR};
			break;
		}
		if(strcmp(qtab[Qcontrol].name, req->wname[0]) != 0)
			goto FileNotExists;
		rep->wqid[0] = (Qid){Qcontrol, 0, 0};
		break;
	default:
		goto WalkInNonDirectory;
	}
	n->qid = rep->wqid[req->nwname-1];
	rep->type = Rwalk;
	rep->nwqid = req->nwname;
	return 1;

WalkInNonDirectory:
	return fs_error(rep, "walk in non directory");
FileNotExists:
	return fs_error(rep, "file does not exist");
}
static int
fs_create(Fcall *req, Fcall *rep)
{
	static int need[4] = {
		4,	/* NP_OREAD */
		2,	/* NP_OWRITE */
		6,	/* NP_ORDWR */
		1	/* NP_OEXEC */
	};
	struct Qtab *t;
	Fid *f;
	int n;

	if(req->mode&NP_OZEROES)
		return fs_error(rep, "invalid 9P2000 open mode");

	f = fid_find(req->fid);
	if(f == nil)
		return fs_error(rep, "bad fid");

	if(f->qid.path != Qposix)
		return fs_eperm(req, rep);

	assert(f->process == nil);
	if(strcmp("signals", req->name) == 0){
		t = qtab + Qsignals;
		n = need[req->mode & 3];
		if((n & (t->mode>>6)) != n)
			return fs_eperm(req, rep);

		n = (int)req->perm;
		if(n == sid){
			/* this is the session leader */
			f->process = session_leader;
			foreground = session_leader->group;
		} else {
			f->process = process_find(n);
			if(f->process){
				if(f->process->flags & CalledExec){
					f->process->flags &= ~CalledExec;
				} else {
					f->process = nil;
					return fs_eperm(req, rep);
				}
			} else {
				f->process = process_create(n);
			}
		}
		f->qid = (Qid){Qsignals, 0, 0};
	} else if(strcmp("nanny", req->name) == 0){
		t = qtab + Qnanny;
		n = need[req->mode & 3];
		if((n & (t->mode>>6)) != n)
			return fs_eperm(req, rep);
		f->process = process_create_nanny((int)req->perm);
		f->qid = (Qid){Qnanny, 0, 0};
	}

	if(f->process == nil)
		return fs_eperm(req, rep);

	f->opened = req->mode;

	/* both processes and nannies share the same iounit */
	rep->type = Rcreate;
	rep->qid = f->qid;
	rep->iounit = sizeof(PosixSignalInfo);

	return 1;
}
static int
fs_open(Fcall *req, Fcall *rep)
{
	static int need[4] = {
		4,	/* NP_OREAD */
		2,	/* NP_OWRITE */
		6,	/* NP_ORDWR */
		1	/* NP_OEXEC */
	};
	struct Qtab *t;
	Fid *f;
	int n;

	if(req->mode&NP_OZEROES)
		return fs_error(rep, "invalid 9P2000 open mode");

	f = fid_find(req->fid);
	if(f == nil)
		return fs_error(rep, "bad fid");
	if(f->opened != -1)
		return fs_error(rep, "already open");

	t = qtab + f->qid.path;
	n = need[req->mode & 3];
	if((n & (t->mode>>6)) != n)
		return fs_eperm(req, rep);

	rep->iounit = 0;
	switch(f->qid.path){
	case Qnanny:
	case Qsignals:
		return fs_enotfound(req, rep);
	case Qcontrol:
		if(control_fid)	/* can be opened only once */
			return fs_enotfound(req, rep);
		control_fid = f;
		rep->iounit = sizeof(PosixSignalInfo);
		break;
	case Qroot:
	case Qposix:
		break;
	default:
		return fs_enotfound(req, rep);
	}
	f->opened = req->mode;
	rep->type = Ropen;
	rep->qid = f->qid;
	return 1;
}
static int
fs_read(Fcall *req, Fcall *rep)
{
	Fid *f;
	Signal *signal;
	Waiter *waiter;

	if(req->count < 0)
		return fs_error(rep, "bad read/jehanne_write count");

	f = fid_find(req->fid);
	if(f == nil){
		return fs_error(rep, "bad fid");
	}
	if(ISCLOSED(f) || f->opened == NP_OWRITE){
		return fs_error(rep, "i/o error");
	}

	rep->type = Rread;
	switch(f->qid.path){
	case Qroot:
		if(req->count == 0){
			rep->count = 0;
			rep->data = nil;
			return 1;
		}
		rep->count = rootread(f, (uint8_t*)data, req->offset, req->count, Maxfdata);
		rep->data = data + req->offset;
		return 1;
	case Qposix:
		/* posix/ is always empty */
		rep->count = 0;
		rep->data = nil;
		return 1;
	case Qsignals:
		assert(f->process != nil);
		if(req->count != sizeof(PosixSignalInfo))
			return fs_error(rep, "i/o error");
		signal = process_find_pending_signal(f->process, req->offset);
		if(signal != nil){
			memmove(data, &signal->info, sizeof(PosixSignalInfo));
			rep->data = data;
			rep->count = sizeof(PosixSignalInfo);
			signal_free(signal);
			f->process->waited = 0;
			return 1;
		}
		waiter = malloc(sizeof(Waiter));
		if(waiter == nil)
			return fs_error(rep, "i/o error");

		f->process->waited = req->offset;
		waiter->tag = req->tag;
		waiter->owner = f;
		waiter->signal = nil;
		waiter->next = waiting_process;
		waiting_process = waiter;
		return 0;
	default:
		return fs_error(rep, "i/o error");
	}
}
static int
fs_write(Fcall *req, Fcall *rep)
{
	/* jehanne_write are always sincronous */
	LongConverter offset;
	union
	{
		int		group;
		PosixSignalMask	mask;
		PosixSignalInfo	signal;
		char		raw[sizeof(PosixSignalInfo)/sizeof(char)];
	} buffer;
	Fid *f;
	Process *p;
	ProcessGroup *g;
	Signal *s;

	if(req->count < 0)
		return fs_error(rep, "bad read/jehanne_write count");

	f = fid_find(req->fid);
	if(f == nil)
		return fs_error(rep, "bad fid");
	if(ISCLOSED(f) || f->opened == NP_OREAD)
		return fs_error(rep, "i/o error");

	rep->count = 0;
	offset.value = req->offset;
	switch(f->qid.path){
	case Qcontrol:
		switch(offset.request.command){
		case PHSignalForeground:
			if(req->count != sizeof(PosixSignalInfo)){
				return fs_error(rep, "cannot send signal to foreground: invalid siginfo");
			}
			if(foreground != nil){
				memmove(buffer.raw, req->data, sizeof(PosixSignalInfo));
				s = signal_create(&buffer.signal);
				group_handle_signal(foreground, s);
				signal_free(s);
			} else {
				DEBUG("fs_write: signal foreground: nothing to do (no foreground process).\n");
			}
			break;
		case PHSignalProcess:
			goto SendSignalToProcess;
		default:
			DEBUG("fs_write: ignoring unknown Qcontrol command %d\n", offset.request.command);
			return fs_error(rep, "i/o error");
		}
		break;
	case Qnanny:
		if(f->process == nil || !(f->process->flags & NannyProcess))
			return fs_error(rep, "i/o error");
		switch(offset.request.command){
		case PHProcessExited:
			/* this is for Nannies only, only for their child */
			if(f->process->cpid != offset.request.target)
				return fs_error(rep, "i/o error");
			p = f->process->child;
			if(p != nil)
				process_free(p);
			break;
		case PHSignalProcess:
			goto SendSignalToProcess;
		default:
			DEBUG("fs_write: ignoring unknown Qnanny command %d\n", offset.request.command);
			return fs_error(rep, "i/o error");
		}
		break;
	case Qsignals:
		/* here we handle commands */
		if(f->process == nil || (f->process->flags & NannyProcess))
			return fs_error(rep, "i/o error");
		switch(offset.request.command){
		case PHCallingExec:
			if(f->process->flags & CalledExec)
				return fs_error(rep, "i/o error");
			f->process->flags |= CalledExec;
			break;
		case PHSetProcessGroup:
			if(offset.request.target == 0)
				p = f->process;
			else {
				p = process_find(offset.request.target);
				if(p == nil)
					return fs_error(rep, "cannot set process group: unknown process");
				if(p != f->process
				&& p->parent != f->process
				&& p->parent != nil && (p->parent->flags&NannyProcess) && p->parent->parent != f->process)
					return fs_eperm(req, rep);
			}
			if(p->flags&SessionLeader)
				return fs_eperm(req, rep);
			g = nil;
			if(req->count == 0){
				g = group_create(p);
			} else if(req->count == sizeof(int)){
				memmove(buffer.raw, req->data, sizeof(int));
				if(buffer.group < 0)
					return fs_error(rep, "cannot set process group: invalid group");
				if(buffer.group == 0 || buffer.group == p->pid)
					g = group_create(p);
				else
					g = group_find(buffer.group);
			} else
				return fs_error(rep, "cannot set process group: invalid group");
			if(g == nil)
				return fs_eperm(req, rep);
			if(p->group != g)
				group_add(g, p);
			break;
		case PHIgnoreSignal:
			if(req->count != sizeof(PosixSignalMask)){
				return fs_error(rep, "cannot set ignore mask: invalid mask");
			}
			memmove(buffer.raw, req->data, sizeof(PosixSignalMask));
			process_ignore_signal(f->process, buffer.mask);
			break;
		case PHEnableSignal:
			if(req->count != sizeof(PosixSignalMask)){
				return fs_error(rep, "cannot set ignore mask: invalid mask");
			}
			memmove(buffer.raw, req->data, sizeof(PosixSignalMask));
			process_enable_signal(f->process, buffer.mask);
			break;
		case PHBlockSignals:
			if(req->count != sizeof(PosixSignalMask)){
				return fs_error(rep, "cannot set block mask: invalid mask");
			}
			memmove(buffer.raw, req->data, sizeof(PosixSignalMask));
			process_block_signals(f->process, buffer.mask);
			break;
		case PHSignalProcess:
SendSignalToProcess:
			if(req->count != sizeof(PosixSignalInfo)){
				return fs_error(rep, "cannot send signal to proces: invalid siginfo");
			}
			memmove(buffer.raw, req->data, sizeof(PosixSignalInfo));
			s = signal_create(&buffer.signal);
			p = process_find(offset.request.target);
			if(p != nil){
				if(process_handle_signal(p, s) == -1){
					process_free(p);
				}
			} else {
				proc_convert_signal(offset.request.target, s);
			}
			signal_free(s);
			break;
		case PHSignalGroup:
			if(req->count != sizeof(PosixSignalInfo)){
				return fs_error(rep, "cannot send signal to group: invalid siginfo");
			}
			memmove(buffer.raw, req->data, sizeof(PosixSignalInfo));
			s = signal_create(&buffer.signal);
			g = group_find(offset.request.target);
			if(g != nil)
				group_handle_signal(g, s);
			signal_free(s);
			break;
		case PHGetProcMask:
			rep->count = f->process->blocked;
			break;
		case PHGetSessionId:
			if(offset.request.target)
				p = process_find(offset.request.target);
			else
				p = f->process;
			if(p != nil)
				rep->count = sid;
			else
				return fs_eperm(req, rep);
			break;
		case PHGetProcessGroupId:
			if(offset.request.target)
				p = process_find(offset.request.target);
			else
				p = f->process;
			if(p == nil){
				// TODO: lookup the group by noteid
				return fs_eperm(req, rep);
			}
			rep->count = p->group->pgid;
			break;
		case PHSetForegroundGroup:
			group_foreground(f->process, offset.request.target);
			break;
		case PHGetForegroundGroup:
			if(foreground)
				rep->count = foreground->pgid;
			else
				rep->count = 2147483647; // max_int;
			break;
		case PHDetachSession:
			if((f->process->flags & SessionLeader)
			|| (f->process->flags & GroupLeader)
			|| (f->process->flags & NannyProcess)
			|| (f->process->flags & Detached))
				return fs_eperm(req, rep);
			if(f->process->children > 0){
				/* we cannot free the process since some
				 * of its children are still in this
				 * session.
				 */
				f->process->flags |= Detached;
			} else {
				process_free(f->process);
				f->process = nil;
			}	
			break;
		case PHGetPendingSignals:
			rep->count = process_pending_signals(f->process);
			break;
		case PHSignalForeground:	/* only in Qcontrol */
		case PHWaitSignals:		/* read */
			return fs_error(rep, "i/o error");
		default:
			DEBUG("fs_write: ignoring unknown Qprocess command %d\n", offset.request.command);
			return fs_error(rep, "i/o error");
		}
		break;
	default:
		return fs_error(rep, "i/o error");
	}
	rep->type = Rwrite;
	return 1;
}
static int
fs_clunk(Fcall *req, Fcall *rep)
{
	Fid *f;
	PosixSignalInfo siginfo;
	Signal *signal;

	f = fid_find(req->fid);
	if(f != nil){
		if(f == external){
			DEBUG("fs_serve: external clients gone\n");
			status = Unmounted;
		} else if(f->qid.path == Qsignals && f->process != nil){
			/* f->process might be nil for detached processes */
			if(f->process == session_leader && foreground != nil){
				// see https://unix.stackexchange.com/questions/407448/
				siginfo.si_signo = PosixSIGHUP;
				siginfo.si_pid = sid;
				signal = signal_create(&siginfo);
				group_handle_signal(foreground, signal);
				signal_free(signal);
			}
			if(!(f->process->flags & CalledExec)){
				if(f->process == session_leader)
					session_leader = nil;
				process_free(f->process);
			}
			f->process = nil;
		} else if(f->qid.path == Qcontrol && session_leader != nil){
			assert(f->process == nil);

			// see https://unix.stackexchange.com/questions/407448/
			siginfo.si_signo = PosixSIGHUP;
			siginfo.si_pid = sid;
			signal = signal_create(&siginfo);
			if(process_handle_signal(session_leader, signal) == -1
			&&((session_leader->flags & CalledExec)||(session_leader->flags & Detached))){
				/* the signal dispatching (aka postnote) failed
				 * and the session_leader called exec.
				 * Let assume that the process exited.
				 */
				process_free(session_leader);
				session_leader = nil;
				if(foreground)
					group_handle_signal(foreground, signal);
			}
			signal_free(signal);
			control_fid = nil;
		} else if(f->qid.path == Qnanny) {
			assert(f->process != nil);
			process_free(f->process);
			f->process = nil;
		}
		f->opened = -1;
	}
	rep->type = Rclunk;
	return 1;
}
static int
fs_stat(Fcall *req, Fcall *rep)
{
	Dir d;
	Fid *f;
	static uint8_t mdata[Maxiosize];

	f = fid_find(req->fid);
	if(f == nil || f->qid.path >= Nqid){
		return fs_error(rep, "bad fid");
	}

	fillstat(f->qid.path, &d);
	rep->type = Rstat;
	rep->nstat = convD2M(&d, mdata, Maxiosize);
	rep->stat = mdata;
	return 1;
}
static int
fs_not_implemented(Fcall *req, Fcall *rep)
{
	return fs_eperm(req, rep);
}
static int (*fcalls[])(Fcall *, Fcall *) = {
	[Tversion]	fs_version,
	[Tauth]		fs_auth,
	[Tattach]	fs_attach,
	[Tflush]	fs_flush,
	[Twalk]		fs_walk,
	[Topen]		fs_open,
	[Tcreate]	fs_create,
	[Tread]		fs_read,
	[Twrite]	fs_write,
	[Tclunk]	fs_clunk,
	[Tremove]	fs_not_implemented,
	[Tstat]		fs_stat,
	[Twstat]	fs_not_implemented,
};


static int
note_forward(void *v, char *s)
{
	PosixSignalInfo siginfo;
	DEBUG("%d: noted: %s\n", *__libposix_pid, s);

	if(strncmp("sys:", s, 4) == 0)
		return 0;	// this is for us...

	// otherwhise it's for the foreground process
	memset(&siginfo, 0, sizeof(PosixSignalInfo));
	if(!__libposix_note_to_signal(s, &siginfo)){
		DEBUG("unable to forward '%s'", s);
		return 1;
	}
	__libposix_sighelper_signal(PHSignalForeground, 0, &siginfo);
	return 1;
}
static void
traceassert(char*a)
{
	char buf[256];
	
	snprint(buf, sizeof(buf), "assert failed: %s, %#p %#p\n", a,
		__builtin_return_address(2),
		__builtin_return_address(3)
	);
	DEBUG(buf);
	exits(buf);
}
void
enabledebug(const char *file)
{
	if (debugging < 0) {
		if((debugging = ocreate(file, OCEXEC|OWRITE, 0666)) < 0)
			sysfatal("ocreate(%s) %r", file);
		__assert = traceassert;
		fmtinstall('F', fcallfmt);
	}
}
static int
readmessage(int fd, Fcall *req)
{
	int n;

	n = read9pmsg(fd, data, Maxiosize);
	if(n > 0)
		if(convM2S((uint8_t*)data, n, req) == 0){
			DEBUG("readmessage: convM2S returns 0\n");
			return -1;
		} else {
			DEBUG("fs_serve: <-%F\n", req);
			return 1;
		}
	if(n < 0){
		DEBUG("readmessage: read9pmsg: %r\n");
		return -1;
	}
	return 0;
}
static int
sendmessage(int fd, Fcall *rep)
{
	int n;
	static uint8_t repdata[Maxiosize];

	n = convS2M(rep, repdata, Maxiosize);
	if(n == 0) {
		DEBUG("sendmessage: convS2M error\n");
		return 0;
	}
	if(jehanne_write(fd, repdata, n) != n) {
		DEBUG("sendmessage: jehanne_write\n");
		return 0;
	}
	DEBUG("fs_serve: ->%F\n", rep);
	return 1;
}
void
fs_serve(int connection)
{
	int r, w, syncrep;
	Fcall rep;
	Fcall *req;
	Waiter *wp, **e;

	process_create(sid);

	req = malloc(sizeof(Fcall)+Maxfdata+((strlen(user)+1)*4));
	if(req == nil)
		sysfatal("out of memory");
	data = malloc(Maxfdata);
	if(data == nil)
		sysfatal("out of memory");

	ftail = &fids;

	status = Initializing;

	DEBUG("started\n");

	do
	{
		DEBUG("wait for a new request\n");
		if((r = readmessage(connection, req)) <= 0){
			DEBUG("readmessage returns %d\n", r);
			goto FSLoopExit;
		}

		rep.tag = req->tag;
		if(req->type < Tversion || req->type > Twstat)
			syncrep = fs_error(&rep, "bad fcall type");
		else
			syncrep = (*fcalls[req->type])(req, &rep);

		if(syncrep){
			if((w = sendmessage(connection, &rep)) <= 0){
				DEBUG("sendmessage returns %d\n", w);
				goto FSLoopExit;
			}
		}

		/* Send available replies to processes waiting in reads */
		e = &waiting_process;
		while(wp = *e){
			if(wp->signal){
				rep.type = Rread;
				rep.tag = wp->tag;
				rep.fid = wp->owner->fd;
				rep.count = sizeof(PosixSignalInfo);
				rep.data = (char*)&wp->signal->info;
				if((w = sendmessage(connection, &rep)) <= 0){
					DEBUG("sendmessage for waiting process returns %d\n", w);
					goto FSLoopExit;
				}
				*e = wp->next;
				wp->owner->process->waited = 0;
				signal_free(wp->signal);
				free(wp);
				continue;
			}
			e = &wp->next;
		}

		/* We can exit (properly) only when the following conditions hold
		 *
		 * - the kernel decided that nobody need us anymore
		 *   (status == Unmounted, see fs_clunk and fs_attach)
		 *
		 * Thus we exit when the kernel decides that nobody will
		 * need our services (aka, all the children sharing the
		 * mountpoint that we serve have exited).
		 *
		 * (AND obviously if an unexpected error occurred)
		 */
	}
	while(status < Unmounted);
FSLoopExit:

	if(r < 0){
		DEBUG("error: fs_serve: readmessage");
		sysfatal("fs_serve: readmessage");
	}
	if(w < 0){
		DEBUG("error: fs_serve: sendmessage");
		sysfatal("fs_serve: sendmessage");
	}

	sys_close(connection);
	DEBUG("sys_close(%d)\n", connection);

	DEBUG("shut down\n");
}

/* Command line / Controller */
static void
tty_from_cons(int fd, int mode)
{
	int tmp;
	char buf[256];

	if(sys_fd2path(fd, buf, sizeof(buf)) < 0)
		sysfatal("fd2path: %d", fd);
	tmp = strlen(buf);
	if(tmp < 9 || strcmp(buf+tmp-9, "/dev/cons") != 0)
		return;
	tmp = sys_open("/dev/tty", mode);
	dup(tmp, fd);
	sys_close(tmp);
}
static void
unmount_dev(void)
{
	char name[256];
	snprint(name, sizeof(name), "#s/posixly.%s.%d", user, sid);
	sys_unmount(name, "/dev");
	sys_remove(name);
}
static void
post_mount(int fd)
{
	/* we want the mount point to be sys_unmount() on session detach,
	 * so it must have a deterministic name: "#s/posixly.glenda.123"
	 * is way better than "#|/data".
	 */
	int f;
	char name[256], buf[32];

	snprint(name, sizeof(name), "#s/posixly.%s.%d", user, sid);
	f = sys_create(name, OWRITE, 0600);
	if(f < 0)
		sysfatal("sys_create(%s)", name);
	sprint(buf, "%d", fd);
	if(jehanne_write(f, buf, strlen(buf)) != strlen(buf))
		sysfatal("jehanne_write(%s)", name);
	sys_close(f);
	sys_close(fd);

	f = sys_open(name, ORDWR);
	if(f < 0)
		sysfatal("sys_open(%s)", name);
	if(sys_mount(f, -1, "/dev", MBEFORE, "", '9') == -1)
		sysfatal("mount: %r");
}
static void
open_control_fd(void)
{
	while((*__libposix_devsignal = sys_open("/dev/posix/control", OWRITE)) < 0)
		sleep(250);
}
void
main(int argc, char *argv[])
{
	int p[2], i, sidprovided, fsrun, leaderrun, controlpid;
	static PosixSignalInfo sighup;
	int devsignal;

	sidprovided = 0;

	ARGBEGIN{
	case 'd':
		enabledebug(EARGF(usage()));
		break;
	case 'p':
		sidprovided = 1;
		sid = atoi(EARGF(usage()));
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(sid == 0 && argc < 1)
		usage();

	sys_rfork(RFFDG);

	controlpid = getpid();
	__libposix_pid = &controlpid;
	user = strdup(getuser());

	if(sid == 0){
		sys_rfork(RFREND|RFNAMEG);

		if(access("/dev/tty", AWRITE|AREAD) == 0){
			/* replace /dev/cons with /dev/tty */
			tty_from_cons(0, OREAD);
			tty_from_cons(1, OWRITE);
			tty_from_cons(2, ORDWR);
			if((i = sys_open("/dev/consctl", OWRITE)) > 0){
				jehanne_write(i, "winchon", 7);
				sys_close(i);
			}
		}

		/* fork session leader */
		switch(sid = sys_rfork(RFPROC|RFNOTEG|RFFDG)){
		case -1:
			sysfatal("rfork");
		case 0:
			while(sys_rendezvous(main, (void*)0x1) == ((void*)~0))
				sleep(250);
			sys_close(debugging);
			jehanne_pexec(strdup(argv[0]), argv);
			exits("exec");
		default:
			break;
		}
	}

	pipe(p);

	switch(fspid = sys_rfork(RFPROC|RFMEM|RFCENVG|RFNOTEG|RFNAMEG|RFNOMNT|RFFDG|RFREND)){
	case -1:
		sysfatal("rfork");
	case 0:
		sys_close(0);
		sys_close(1);
		sys_close(2);
		sys_close(p[0]);
		fspid = getpid();
		fs_serve(p[1]);
		exits(nil);
	default:
		break;
	}

	sys_close(0);
	sys_close(1);
	sys_close(2);
	sys_close(p[1]);
	post_mount(p[0]);

	__libposix_devsignal = &devsignal;
	open_control_fd();

	sys_rfork(RFCNAMEG);

	if(!atnotify(note_forward, 1)){
		fprint(2, "atnotify: %r\n");
		exits("atnotify");
	}

	if(!sidprovided){
		/* let the session leader start */
		while(sys_rendezvous(main, (void*)0x2) == ((void*)~0))
			sleep(250);
		/* if we created the session leader, we will wait for it */
		leaderrun = 1;
	}

	/* We wait for fspid because it will be alive until any process
	 * will be in the namespace providing /dev/posix/.
	 * Indeed until there are process in such namespace we
	 * want to forward notes/signals to their foreground group.
	 *
	 * Also we wait for sid because if the session leader exits
	 * we have to send SIGHUP to the foreground group
	 */
	fsrun = 1;
	while(fsrun || leaderrun){
		i = waitpid();
		if(i == fspid){
			DEBUG("file system exited\n");
			fsrun = 0;
			leaderrun = 0; /* no need to wait for the leader */
		} else if(i == sid){
			DEBUG("session leader exited\n");
			sighup.si_signo = PosixSIGHUP;
			__libposix_sighelper_signal(PHSignalForeground, 0, &sighup);
			sys_close(devsignal);
			unmount_dev();
			leaderrun = 0;
		}
	}
	exits(nil);
}
