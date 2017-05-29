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

static PosixStatReader __libposix_stat_reader;
static PosixOpenTranslator __libposix_open_translation;

typedef enum SeekTypes
{
	SeekSet		= 0,
	SeekCurrent	= 1,
	SeekEnd		= 2
} SeekTypes;
static int *__libposix_seek_types;

void
__libposix_files_check_conf(void)
{
	if(__libposix_stat_reader == nil)
		sysfatal("libposix: no stat reader");
	if(__libposix_open_translation == nil)
		sysfatal("libposix: no open translator");
	if(__libposix_seek_types == nil)
		sysfatal("libposix: no seek translations");
}

int
libposix_translate_open(PosixOpenTranslator translation)
{
	if(__libposix_initialized())
		return 0;
	if(translation == nil)
		return 0;
	__libposix_open_translation = translation;
	return 1;
}

int
libposix_set_stat_reader(PosixStatReader reader)
{
	if(__libposix_initialized())
		return 0;
	if(reader == nil)
		return 0;
	__libposix_stat_reader = reader;
	return 1;
}

int
libposix_translate_seek_whence(int seek_set, int seek_cur, int seek_end)
{
	if(__libposix_initialized())
		return 0;
	if(seek_set == seek_cur)
		return 0;
	if(seek_set == seek_end)
		return 0;
	if(seek_cur == seek_end)
		return 0;
	__libposix_seek_types = malloc(sizeof(int)*3);
	if(__libposix_seek_types == nil)
		return 0;

	__libposix_seek_types[SeekSet] = seek_set;
	__libposix_seek_types[SeekCurrent] = seek_cur;
	__libposix_seek_types[SeekEnd] = seek_end;
	return 1;
}

static SeekTypes
find_seek_type(int whence)
{
	if(__libposix_seek_types[SeekSet] == whence)
		return SeekSet;
	if(__libposix_seek_types[SeekCurrent] == whence)
		return SeekCurrent;
	if(__libposix_seek_types[SeekEnd] == whence)
		return SeekEnd;
	return -1;
}

int
POSIX_pipe(int *errnop, int fildes[2])
{
	int res;
	res = jehanne_pipe(fildes);
	if(res == -1)
		*errnop = __libposix_get_errno(PosixEMFILE);
	return res;
}

int
POSIX_open(int *errnop, const char *name, int flags, int mode)
{
	long omode = 0, cperm = 0;
	PosixError e;
	int f;

	if(name == nil){
		*errnop = __libposix_get_errno(PosixENOENT);
		return -1;
	}
	e = __libposix_open_translation(flags, mode, &omode, &cperm);
	if(e != 0){
		*errnop = __libposix_get_errno(e);
		return -1;
	}
	if(cperm == 0){
		f = sys_open(name, omode);
	} else {
		f = ocreate(name, (unsigned int)omode, (unsigned int)cperm);
	}
	if(f >= 0)
		return f;
	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_open);
	return -1;
}

long
POSIX_read(int *errnop, int fd, char *buf, size_t len)
{
	long r;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
SignalReenter:
	r = sys_pread(fd, buf, len, -1);
	if(r < 0){
		if(__libposix_restart_syscall())
			goto SignalReenter;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_read);
		return -1;
	}
	return r;
}

long
POSIX_write(int *errnop, int fd, const void *buf, size_t len)
{
	long w;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
SignalReenter:
	w = sys_pwrite(fd, buf, len, -1);
	if(w < 0){
		if(__libposix_restart_syscall())
			goto SignalReenter;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_write);
		return -1;
	}
	return w;
}

off_t
POSIX_lseek(int *errnop, int fd, off_t pos, int whence)
{
	SeekTypes stype;
	long r;

	stype = find_seek_type(whence);
	if(stype == -1){
		*errnop = __libposix_get_errno(PosixEINVAL);
		return -1;
	}
	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
	r = sys_seek(fd, pos, stype);
	if(r >= 0)
		return r;
	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_lseek);
	return -1;
}

int
POSIX_close(int *errno, int file)
{
	long ret;

	ret = sys_close(file);
	switch(ret){
	case 0:
		return 0;
	case ~0:
		*errno = __libposix_translate_errstr((uintptr_t)POSIX_close);
		break;
	default:
		*errno = ret;
		break;
	}
	return -1;
}

int
POSIX_link(int *errnop, const char *old, const char *new)
{
	int err;
	/* let choose the most appropriate error */
	if(old == nil || new == nil || old[0] == 0 || new[0] == 0)
		err = __libposix_get_errno(PosixENOENT);
	else if(access(new, AEXIST) == 0)
		err = __libposix_get_errno(PosixEEXIST);
	else if(access(old, AEXIST) == 0)
		err = __libposix_get_errno(PosixENOENT);
	else {
		/* Jehanne does not support links.
		 * A custom overlay filesystem might support them in
		 * the future but so far it does not exists yet.
		 *
		 * We return EXDEV so that a posix compliant caller
		 * can fallback to a simple copy.
		 */
		err = __libposix_get_errno(PosixEXDEV);
	}
	*errnop = err;
	return -1;
}

int
POSIX_unlink(int *errnop, const char *name)
{
	long ret;
	if(name == nil || name[0] == 0){
		*errnop = __libposix_get_errno(PosixENOENT);
		return -1;
	}
	ret = sys_remove(name);
	switch(ret){
	case 0:
		return 0;
	case ~0:
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_unlink);
		break;
	default:
		*errnop = ret;
		break;
	}
	return -1;
}

int
POSIX_fstat(int *errnop, int file, void *pstat)
{
	Dir *d;
	PosixError e;
	if(pstat == nil){
		*errnop = __libposix_get_errno(PosixEOVERFLOW);
		return -1;
	}
	if(file < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
SignalReenter:
	d = dirfstat(file);
	if(d == nil)
	{
		if(__libposix_restart_syscall())
			goto SignalReenter;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_fstat);
		return -1;
	}
	e = __libposix_stat_reader(pstat, d);
	free(d);
	if(e == 0)
		return 0;
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_stat(int *errnop, const char *file, void *pstat)
{
	Dir *d;
	PosixError e;
	if(pstat == nil){
		*errnop = __libposix_get_errno(PosixEOVERFLOW);
		return -1;
	}
	if(file == nil){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
	d = dirstat(file);
	if(d == nil)
	{
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_stat);
		return -1;
	}
	e = __libposix_stat_reader(pstat, d);
	free(d);
	if(e == 0)
		return 0;
	*errnop = __libposix_get_errno(e);
	return -1;
}
