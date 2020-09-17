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
#include <9P2000.h>
#include <posix.h>
#include "internal.h"

static PosixStatReader __libposix_stat_reader;
static PosixOpenTranslator __libposix_open_translation;
static int __libposix_AT_FDCWD;
static int __libposix_F_OK;
static int __libposix_R_OK;
static int __libposix_W_OK;
static int __libposix_X_OK;

static int *__libposix_coe_fds;
static int __libposix_coe_fds_size;
static int __libposix_coe_fds_used;

int __libposix_O_NONBLOCK;
static int *__libposix_nb_fds;
static int __libposix_nb_fds_size;
static int __libposix_nb_fds_used;

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

void
__libposix_set_close_on_exec(int fd, int close)
{
	int i;
	if(__libposix_coe_fds_size == __libposix_coe_fds_used){
		__libposix_coe_fds_size += 8;
		__libposix_coe_fds = realloc(__libposix_coe_fds, __libposix_coe_fds_size * sizeof(int));
		i = __libposix_coe_fds_size;
		while(i > __libposix_coe_fds_used)
			__libposix_coe_fds[--i] = -1;
	}
	/* remove fd if already present */
	i = 0;
	while(i < __libposix_coe_fds_size){
		if(__libposix_coe_fds[i] == fd){
			__libposix_coe_fds[i] = -1;
			--__libposix_coe_fds_used;
			break;
		}
		++i;
	}
	if(close){
		/* add fd to close on exec */
		i = 0;
		while(i < __libposix_coe_fds_size){
			if(__libposix_coe_fds[i] == -1){
				__libposix_coe_fds[i] = fd;
				break;
			}
			++i;
		}
		++__libposix_coe_fds_used;
	}
}

void
__libposix_close_on_exec(void)
{
	int i = 0;
	while(i < __libposix_coe_fds_size){
		if(__libposix_coe_fds[i] != -1){
			sys_close(__libposix_coe_fds[i]);
			__libposix_coe_fds[i] = -1;
			--__libposix_coe_fds_used;
		}
		++i;
	}
	assert(__libposix_coe_fds_used == 0);
	free(__libposix_coe_fds);
	__libposix_coe_fds = nil;
	__libposix_coe_fds_size = 0;
}

int
__libposix_should_close_on_exec(int fd)
{
	int i = 0;
	while(i < __libposix_coe_fds_size){
		if(__libposix_coe_fds[i] == fd)
			return 1;
		++i;
	}
	return 0;
}

void
__libposix_set_non_blocking(int fd, int enable)
{
	int i;
	if(__libposix_nb_fds_size == __libposix_nb_fds_used){
		__libposix_nb_fds_size += 8;
		__libposix_nb_fds = realloc(__libposix_nb_fds, __libposix_nb_fds_size * sizeof(int));
		i = __libposix_nb_fds_size;
		while(i > __libposix_nb_fds_used)
			__libposix_nb_fds[--i] = -1;
	}
	/* remove fd if already present */
	i = 0;
	while(i < __libposix_nb_fds_size){
		if(__libposix_nb_fds[i] == fd){
			__libposix_nb_fds[i] = -1;
			--__libposix_nb_fds_used;
			break;
		}
		++i;
	}
	if(enable){
		/* add fd to ensure it will not block */
		i = 0;
		while(i < __libposix_nb_fds_size){
			if(__libposix_nb_fds[i] == -1){
				__libposix_nb_fds[i] = fd;
				break;
			}
			++i;
		}
		++__libposix_nb_fds_used;
	}
}

int
__libposix_should_not_block(int fd)
{
	int i = 0;
	while(i < __libposix_nb_fds_size){
		if(__libposix_nb_fds[i] == fd)
			return 1;
		++i;
	}
	return 0;
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

int
libposix_translate_access_mode(int f_ok, int r_ok, int w_ok, int x_ok)
{
	if(__libposix_initialized())
		return 0;

	__libposix_F_OK = f_ok;
	__libposix_R_OK = r_ok;
	__libposix_W_OK = w_ok;
	__libposix_X_OK = x_ok;
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
POSIX_access(int *errnop, const char *path, int amode)
{
	Dir *d;
	PosixError e = 0;
	
	d = dirstat(path);
	if(path == nil || path[0] == '\0'){
		e = PosixENOENT;
	} else if(d == nil){
		e = PosixENOENT;
	} else if(amode == __libposix_F_OK){
		goto AccessDone;
	} else if(amode & ~(__libposix_R_OK|__libposix_W_OK|__libposix_X_OK)){
		e = PosixEINVAL;
	} else if((amode & __libposix_R_OK) && access(path, AREAD) != 0){
		e = PosixEACCES;
	} else if((d->mode & DMDIR) != 0){
		/* we lie, but on Plan 9 access(AWRITE) and access(AEXEC)
		 * will always fail on directories.
		 */
		goto AccessDone;
	} else if((amode & __libposix_W_OK) && access(path, AWRITE) != 0){
		e = PosixEACCES;
	} else if((amode & __libposix_X_OK) && access(path, AEXEC) != 0){
		e = PosixEACCES;
	}else{
AccessDone:
		free(d);
		return 0;
	}

	free(d);
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_dup2(int *errnop, int fildes, int fildes2)
{
	int newfd;
	if(fildes < 0 || fildes2 < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
	newfd = dup(fildes, fildes2);
	if(newfd < 0){
		*errnop = __libposix_get_errno(PosixEINTR);
		return -1;
	}
	return newfd;
}

int
POSIX_dup(int *errnop, int fildes)
{
	int newfd;
	if(fildes < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
	newfd = dup(fildes, -1);
	if(newfd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
	return newfd;
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
	Dir *d;
	int f;

	if(name == nil){
		*errnop = __libposix_get_errno(PosixENOENT);
		return -1;
	}
	e = __libposix_open_translation(flags, mode, &omode, &cperm);
	if(e != 0)
		goto FailWithError;
	if(omode & DMDIR){
		d = dirstat(name);
		if(d != nil){
			if((d->mode & DMDIR) == 0)
				e = PosixENOTDIR;
			free(d);
		}
		if(e != 0)
			goto FailWithError;
		omode &= ~DMDIR;
	}
	if(cperm == 0){
		f = sys_open(name, omode);
	} else {
		f = ocreate(name, (unsigned int)omode, (unsigned int)cperm);
	}
	if(f >= 0){
		if(flags & __libposix_O_NONBLOCK)
			__libposix_set_non_blocking(f, 1);
		return f;
	}
	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_open);
	return -1;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

long
POSIX_read(int *errnop, int fd, char *buf, size_t len)
{
	long r, wkp = 0;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
OnIgnoredSignalInterrupt:
	if(__libposix_should_not_block(fd))
		wkp = sys_awake(2);
	r = sys_pread(fd, buf, len, -1);
	if(r < 0){
		if(wkp){
			if(!awakened(wkp))
				forgivewkp(wkp);
			*errnop = __libposix_get_errno(PosixEAGAIN);
			return -1;
		}
		if(__libposix_restart_syscall())
			goto OnIgnoredSignalInterrupt;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_read);
		return -1;
	}
	if(wkp)
		forgivewkp(wkp);
	return r;
}

long
POSIX_write(int *errnop, int fd, const void *buf, size_t len)
{
	long w, wkp = 0;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
OnIgnoredSignalInterrupt:
	if(__libposix_should_not_block(fd))
		wkp = sys_awake(2);
	w = sys_pwrite(fd, buf, len, -1);
	if(w < 0){
		if(wkp){
			if(!awakened(wkp))
				forgivewkp(wkp);
			*errnop = __libposix_get_errno(PosixEAGAIN);
			return -1;
		}
		if(__libposix_restart_syscall())
			goto OnIgnoredSignalInterrupt;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_write);
		return -1;
	}
	if(wkp)
		forgivewkp(wkp);
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

long
POSIX_pread(int *errnop, int fd, char *buf, size_t len, long offset)
{
	long r, wkp = 0;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
OnIgnoredSignalInterrupt:
	if(__libposix_should_not_block(fd))
		wkp = sys_awake(2);
	r = sys_pread(fd, buf, len, offset);
	if(r < 0){
		if(wkp){
			if(!awakened(wkp))
				forgivewkp(wkp);
			*errnop = __libposix_get_errno(PosixEAGAIN);
			return -1;
		}
		if(__libposix_restart_syscall())
			goto OnIgnoredSignalInterrupt;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_read);
		return -1;
	}
	if(wkp)
		forgivewkp(wkp);
	return r;
}

long
POSIX_pwrite(int *errnop, int fd, const char *buf, size_t len, long offset)
{
	long w, wkp = 0;

	if(fd < 0){
		*errnop = __libposix_get_errno(PosixEBADF);
		return -1;
	}
OnIgnoredSignalInterrupt:
	if(__libposix_should_not_block(fd))
		wkp = sys_awake(2);
	w = sys_pwrite(fd, buf, len, offset);
	if(w < 0){
		if(wkp){
			if(!awakened(wkp))
				forgivewkp(wkp);
			*errnop = __libposix_get_errno(PosixEAGAIN);
			return -1;
		}
		if(__libposix_restart_syscall())
			goto OnIgnoredSignalInterrupt;
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_write);
		return -1;
	}
	if(wkp)
		forgivewkp(wkp);
	return w;
}

int
POSIX_close(int *errnop, int file)
{
	long ret;

	ret = sys_close(file);
	switch(ret){
	case 0:
		return 0;
	case ~0:
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_close);
		break;
	default:
		*errnop = ret;
		break;
	}
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
POSIX_rmdir(int *errnop, const char *name)
{
	Dir *db;
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
		db = nil;
		if((db=dirstat(name))!=nil && (db->qid.type&QTDIR)){
			*errnop = __libposix_translate_errstr((uintptr_t)POSIX_rmdir);
		} else {
			*errnop = __libposix_get_errno(PosixENOTDIR);
		}
		free(db);
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
OnIgnoredSignalInterrupt:
	d = dirfstat(file);
	if(d == nil){
		if(__libposix_restart_syscall())
			goto OnIgnoredSignalInterrupt;
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
	if(d == nil){
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

int
POSIX_chmod(int *errnop, const char *path, int mode)
{
	Dir *dir, ndir;
	long cperm = 0;
	PosixError e;

	if(path == nil)
		e = PosixENOENT;
	else
		e = __libposix_open_translation(0, mode, nil, &cperm);
	if(e != 0)
		goto FailWithError;
	dir = dirstat(path);
	if(dir == nil){
		e = PosixENOENT;
		goto FailWithError;
	}
	nulldir(&ndir);
	ndir.mode = (dir->mode & ~(0777)) | (cperm & 0777);
	free(dir);

	if(dirwstat(path, &ndir) < 0){
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_chmod);
		return -1;
	}

	return 0;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_fchmodat(int *errnop, int fd, const char *path, long mode, int flag)
{
	Dir *dir, ndir;
	long cperm = 0;
	char fullpath[4096], *p;
	int l;
	PosixError e;

	if(fd == __libposix_AT_FDCWD){
		return POSIX_chmod(errnop, path, mode);
	} else if(path == nil){
		e = PosixENOENT;
	} else if(strlen(path) > sizeof(fullpath)-100){
		e = PosixENAMETOOLONG;
	} else if(path[0] == '/'){
		if(fd < 0)
			e = PosixEBADF;
		else {
			dir = dirfstat(fd);
			if(dir == nil){
				e = PosixEBADF;
			} else {
				if((dir->mode & DMDIR) == 0)
					e = PosixENOTDIR;
				free(dir);
			}
		}
	} else {
		e = __libposix_open_translation(0, mode, nil, &cperm);
	}
	if(e == 0 && sys_fd2path(fd, fullpath, sizeof(fullpath)-2) != 0)
		e = PosixEBADF;
	if(e != 0)
		goto FailWithError;

	l = strlen(fullpath);
	fullpath[l++] = '/';
	p = fullpath + l;
	l = strlen(path);
	if(l > sizeof(fullpath) - (p - fullpath) - 1){
		e = PosixENAMETOOLONG;
		goto FailWithError;
	}
	strncpy(p, path, strlen(path)+1);
	cleanname(fullpath);

	dir = dirstat(path);
	if(dir == nil){
		e = PosixENOENT;
		goto FailWithError;
	}
	nulldir(&ndir);
	ndir.mode = (dir->mode & ~(0777)) | (cperm & 0777);
	free(dir);

	if(dirwstat(path, &ndir) < 0){
		*errnop = __libposix_translate_errstr((uintptr_t)POSIX_chmod);
		return -1;
	}

	return 0;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_chown(int *errnop, const char *pathname, int owner, int group)
{
	/* TODO: implement when actually needed */
	return 0;
}

int
POSIX_fchownat(int *errnop, int fd, const char *path, int owner, int group, int flag)
{
	/* TODO: implement when actually needed */
	return 0;
}

int
POSIX_chdir(int *errnop, const char *path)
{
	Dir *d = nil;
	PosixError e = 0;

	if(path == nil){
		e = PosixEFAULT;
	} else {
		d = dirstat(path);
		if(d == nil){
			e = PosixENOENT;
		} else {
			if((d->mode & DMDIR) == 0)
				e = PosixENOTDIR;
			free(d);
		}
	}
	if(e != 0)
		goto FailWithError;

	if(chdir(path) == 0)
		return 0;

	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_chdir);
	return -1;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_fchdir(int *errnop, int fd)
{
	Dir *d;
	PosixError e = 0;
	char buf[4096];

	if(fd < 0){
		e = PosixEBADF;
	} else {
		d = dirfstat(fd);
		if(d == nil){
			e = PosixENOENT;
		} else {
			if((d->mode & DMDIR) == 0)
				e = PosixENOTDIR;
			free(d);
			if(sys_fd2path(fd, buf, sizeof(buf)) != 0)
				e = PosixENOENT;	/* just removed? */
		}
	}
	if(e != 0)
		goto FailWithError;

	if(chdir(buf) == 0)
		return 0;

	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_fchdir);
	return -1;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_rename(int *errnop, const char *from, const char *to)
{
	int n, ffd = -1, tfd = -1;
	const char *f, *t;
	char buf[8192];
	PosixError e = 0;
	Dir *d, nd;

	if(from == nil || to == nil){
		e = PosixENOENT;
		goto FailWithError;
	}

	f = strrchr(from, '/');
	t = strrchr(to, '/');
	f = f ? f+1 : from;
	t = t ? t+1 : to;

	if(strcmp(".", f) == 0 || strcmp("..", f) == 0
	|| strcmp(".", t) == 0 || strcmp("..", t) == 0){
		e = PosixEINVAL;
		goto FailWithError;
	}
	if(access(to, AEXIST) == 0){
		if(sys_remove(to) < 0){
			e = PosixEACCES;
			goto FailWithError;
		}
	}
	if((d = dirstat(from)) == nil){
		e = PosixENOENT;
	} else if(f-from == t-to && strncmp(from, to, f-from) == 0){
		/* from and to are in same directory (we miss some cases) */
		nulldir(&nd);
		nd.name = (char*)t;
		if(dirwstat(from, &nd) < 0)
			e = PosixEPERM;
	} else {
		/* different directories: with 9P2000 we have to copy */
		if((ffd = sys_open(from, OREAD)) < 0
		|| (tfd = sys_create(to, OWRITE, d->mode)) < 0){
			e = PosixEPERM;
		}
		while(e == 0 && (n = jehanne_read(ffd, buf, sizeof(buf))) > 0)
			if(jehanne_write(tfd, buf, n) != n)
				e = PosixEIO;
		if(ffd >= 0)
			sys_close(ffd);
		if(tfd >= 0)
			sys_close(tfd);
		if(e == 0 && sys_remove(from) < 0)
			e = PosixEIO;
		if(e == PosixEIO && tfd >= 0)
			sys_remove(to);
	}
	if(e != 0)
		goto FailWithError;
	free(d);
	return 0;

FailWithError:
	if(d != nil)
		free(d);
	*errnop = __libposix_get_errno(e);
	return -1;
}

int
POSIX_mkdir(int *errnop, const char *path, int mode)
{
	long cperm = 0;
	PosixError e = 0;
	int fd;

	if(path == nil)
		e = PosixEFAULT;
	else if(access(path, AEXIST) == 0)
		e = PosixEEXIST;
	else
		e = __libposix_open_translation(0, mode, nil, &cperm);
	if(e != 0)
		goto FailWithError;

	fd = sys_create(path, OREAD, DMDIR | cperm);
	if(fd >= 0){
		sys_close(fd);
		return 0;
	}

	*errnop = __libposix_translate_errstr((uintptr_t)POSIX_mkdir);
	return -1;

FailWithError:
	*errnop = __libposix_get_errno(e);
	return -1;
}


/* getdents is not POSIX, but is used in POSIX C library to implement
 * readdir.
 */
int
libposix_getdents(int *errnop, int fd, char *buf, int buf_bytes)
{
#define MINSTATLEN	(STATFIXLEN + 4) /* 4 = min (1 char) name, uid, gid, muid */

	int r;
	Dir *d;
	struct dirent *dp;
	char *p;

	if(fd < 0)
		goto FailWithEBADF;
	if(buf == nil)
		goto FailWithEFAULT;
	if(buf_bytes < MINSTATLEN)
		goto FailWithEINVAL;

	d = dirfstat(fd);
	if(d == nil)
		goto FailWithENOENT;	/* removed? */
	r = d->mode & DMDIR;
	free(d);

	if(r == 0)
		goto FailWithENOTDIR;	/* not a directory */

	r = jehanne_read(fd, buf, buf_bytes);
	if(r == 0)
		return 0;
	if(r < 0)
		goto FailWithENOENT;	/* removed? */
	if(r < MINSTATLEN)
		goto FailWithEIO;

	p = buf;
	while(p - buf < r){
		/* here we adjust 9P2000 stat info to match
		 * dirent specification
		 */
		dp = (struct dirent *)p;

		/* the count field in stat structure exclude itself */
		dp->d_reclen += BIT16SZ;

		/* 9P2000 strings are not NUL-terminated */
		dp->d_name[dp->d_namlen] = '\0';

		/* we have to map types to UNIX */
		if(dp->d_type & (DMDIR >> 24))
			dp->d_type = DT_DIR;
		else if(dp->__pad1__ == '|')	/* kernel device == devpipe */
			dp->d_type = DT_FIFO;
		else
			dp->d_type = DT_REG;	/* UNIX lack fantasy :-) */

		p += dp->d_reclen;
	}

	return r;

FailWithEBADF:
	*errnop = __libposix_get_errno(PosixEBADF);
	return -1;

FailWithEFAULT:
	*errnop = __libposix_get_errno(PosixEFAULT);
	return -1;

FailWithEINVAL:
	*errnop = __libposix_get_errno(PosixEINVAL);
	return -1;

FailWithENOENT:
	*errnop = __libposix_get_errno(PosixENOENT);
	return -1;

FailWithENOTDIR:
	*errnop = __libposix_get_errno(PosixENOTDIR);
	return -1;

FailWithEIO:
	*errnop = __libposix_get_errno(PosixEIO);
	return -1;
}

int
libposix_define_at_fdcwd(int AT_FDCWD)
{
	if(__libposix_AT_FDCWD != 0)
		return -1;
	__libposix_AT_FDCWD = AT_FDCWD;
	return 0;
}

int
libposix_define_ononblock(int O_NONBLOCK)
{
	if(__libposix_O_NONBLOCK != 0)
		return -1;
	__libposix_O_NONBLOCK = O_NONBLOCK;
	return 0;
}
