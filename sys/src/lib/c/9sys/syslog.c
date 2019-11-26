/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>

static struct
{
	int	fd;
	int	consfd;
	char	*name;
	Dir	*d;
	Dir	*consd;
	Lock;
} sl =
{
	-1, -1,
};

static void
_syslogopen(void)
{
	char buf[1024];

	if(sl.fd >= 0)
		sys_close(sl.fd);
	jehanne_snprint(buf, sizeof(buf), "/sys/log/%s", sl.name);
	sl.fd = sys_open(buf, OWRITE|OCEXEC);
}

static int
eqdirdev(Dir *a, Dir *b)
{
	return a != nil && b != nil &&
		a->dev == b->dev && a->type == b->type &&
		a->qid.path == b->qid.path;
}

/*
 * Print
 *  sysname: time: mesg
 * on /sys/log/logname.
 * If cons or log file can't be opened, print on the system console, too.
 */
void
jehanne_syslog(int cons, const char *logname, const char *fmt, ...)
{
	char buf[1024];
	char *ctim, *p;
	va_list arg;
	int n;
	Dir *d;
	char err[ERRMAX];

	err[0] = '\0';
	sys_errstr(err, sizeof err);
	jehanne_lock(&sl);

	/*
	 *  paranoia makes us stat to make sure a fork+close
	 *  hasn't broken our fd's
	 */
	d = jehanne_dirfstat(sl.fd);
	if(sl.fd < 0 || sl.name == nil || jehanne_strcmp(sl.name, logname) != 0 ||
	   !eqdirdev(d, sl.d)){
		jehanne_free(sl.name);
		sl.name = jehanne_strdup(logname);
		if(sl.name == nil)
			cons = 1;
		else{
			jehanne_free(sl.d);
			sl.d = nil;
			_syslogopen();
			if(sl.fd < 0)
				cons = 1;
			else
				sl.d = jehanne_dirfstat(sl.fd);
		}
	}
	jehanne_free(d);
	if(cons){
		d = jehanne_dirfstat(sl.consfd);
		if(sl.consfd < 0 || !eqdirdev(d, sl.consd)){
			jehanne_free(sl.consd);
			sl.consd = nil;
			sl.consfd = sys_open("#c/cons", OWRITE|OCEXEC);
			if(sl.consfd >= 0)
				sl.consd = jehanne_dirfstat(sl.consfd);
		}
		jehanne_free(d);
	}

	if(fmt == nil){
		jehanne_unlock(&sl);
		return;
	}

	ctim = jehanne_ctime(jehanne_time(0));
	p = buf + jehanne_snprint(buf, sizeof(buf)-1, "%s ", jehanne_sysname());
	jehanne_strncpy(p, ctim+4, 15);
	p += 15;
	*p++ = ' ';
	sys_errstr(err, sizeof err);
	va_start(arg, fmt);
	p = jehanne_vseprint(p, buf+sizeof(buf)-1, fmt, arg);
	va_end(arg);
	*p++ = '\n';
	n = p - buf;

	if(sl.fd >= 0){
		sys_seek(sl.fd, 0, 2);
		jehanne_write(sl.fd, buf, n);
	}

	if(cons && sl.consfd >=0)
		jehanne_write(sl.consfd, buf, n);

	jehanne_unlock(&sl);
}
