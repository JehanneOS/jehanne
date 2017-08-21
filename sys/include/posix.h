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

/*
 * This API is designed to help porting other POSIX compliant C
 * libraries (such as newlib or musl) to Jehanne, so that they can be
 * used to port further software.
 *
 * POSIX_* functions provide a facade between the Jehanne's libc and
 * the POSIX 1-2008 semantics.
 *
 * libposix_* functions provide non-standard widely used functions
 * (eg. libposix_getdents) and configuration points.
 *
 * #include <u.h>
 * #include <posix.h>
 *
 * Defining _LIBPOSIX_H before the include allow you to just get dirent
 * definition.
 */

#ifndef _LIBPOSIX_DIRENT
#define _LIBPOSIX_DIRENT

/* dirent alias of stat(5) message.
 * We (ab)use the fact that both 9P and Jehanne are little endian.
 * With the new file protocol this might change.
 */
struct __attribute__((__packed__)) dirent
{
	unsigned short	d_reclen;
	unsigned short	__pad1__;	/* don't use */
	unsigned int	__pad2__;	/* don't use */
	unsigned char	d_type;
	unsigned int	d_version;
	unsigned long	d_ino;
	unsigned int	d_mode;
	unsigned int	__pad3__;	/* don't use */
	unsigned int	__pad4__;	/* don't use */
	unsigned long	d_filesize;
	unsigned short	d_namlen;
	char		d_name[];
};

#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_TYPE
#define _DIRENT_HAVE_D_VERSION
#define _DIRENT_HAVE_D_MODE
#define _DIRENT_HAVE_D_FILESIZE
#define _DIRENT_HAVE_D_NAMLEN
#undef _DIRENT_HAVE_D_OFF

#define	DT_UNKNOWN	 0
#define	DT_FIFO		 1
#define	DT_CHR		 2
#define	DT_DIR		 4
#define	DT_BLK		 6
#define	DT_REG		 8
#define	DT_LNK		10
#define	DT_SOCK		12
#define	DT_WHT		14

#endif /* _LIBPOSIX_DIRENT */

#ifndef _LIBPOSIX_H
#define _LIBPOSIX_H

typedef unsigned long clock_t;

#define __POSIX_EXIT_PREFIX "posix error "
#define __POSIX_EXIT_SIGNAL_PREFIX "terminated by posix signal "
#define __POSIX_SIGNAL_PREFIX "posix: "

extern void POSIX_exit(int code) __attribute__((noreturn));
extern int POSIX_close(int *errnop, int file);
extern int POSIX_execve(int *errnop, const char *name, char * const*argv, char * const*env);
extern int POSIX_fork(int *errnop);
extern int POSIX_fstat(int *errnop, int file, void *stat);
extern char* POSIX_getcwd(int *errnop, char *buf, int size);
extern char* POSIX_getlogin(int *errnop);
extern int POSIX_getlogin_r(int *errnop, char *name, int namesize);
extern char* POSIX_getpass(int *errnop, const char *prompt);
extern int POSIX_getpid(int *errnop);
extern int POSIX_getppid(int *errnop);
extern int POSIX_isatty(int *errnop, int file);
extern int POSIX_kill(int *errnop, int pid, int sig);
extern int POSIX_link(int *errnop, const char *old, const char *new);
extern off_t POSIX_lseek(int *errnop, int fd, off_t pos, int whence);
extern int POSIX_open(int *errnop, const char *name, int flags, int mode);
extern long POSIX_pread(int *errnop, int fd, char *buf, size_t len, long offset);
extern long POSIX_pwrite(int *errnop, int fd, const char *buf, size_t len, long offset);
extern long POSIX_read(int *errnop, int fd, char *buf, size_t len);
extern int POSIX_stat(int *errnop, const char *file, void *stat);
extern clock_t POSIX_times(int *errnop, void *tms);
extern int POSIX_unlink(int *errnop, const char *name);
extern int POSIX_wait(int *errnop, int *status);
extern int POSIX_waitpid(int *errnop, int pid, int *status, int options);
extern long POSIX_write(int *errnop, int fd, const void *buf, size_t len);
extern int POSIX_gettimeofday(int *errnop, void *timeval, void *timezone);
extern char* POSIX_getenv(int *errnop, const char *name);
extern void *POSIX_sbrk(int *errnop, ptrdiff_t incr);
extern void * POSIX_malloc(int *errnop, size_t size);
extern void *POSIX_realloc(int *errnop, void *ptr, size_t size);
extern void *POSIX_calloc(int *errnop, size_t nelem, size_t size);
extern void POSIX_free(void *ptr);
extern unsigned int POSIX_sleep(unsigned int seconds);
extern int POSIX_pipe(int *errnop, int fildes[2]);

extern int libposix_getdents(int *errnop, int fd, char *buf, int buf_bytes);

/* Library initialization
 */
#define _ERRNO_H	// skip the Posix part, we just need the enum
#include <apw/errno.h>

typedef enum PosixSignals
{
	PosixSIGABRT = 1,
	PosixSIGALRM,
	PosixSIGBUS,
	PosixSIGCHLD,
	PosixSIGCONT,
	PosixSIGFPE,
	PosixSIGHUP,
	PosixSIGILL,
	PosixSIGINT,
	PosixSIGKILL,
	PosixSIGPIPE,
	PosixSIGQUIT,
	PosixSIGSEGV,
	PosixSIGSTOP,
	PosixSIGTERM,
	PosixSIGTSTP,
	PosixSIGTTIN,
	PosixSIGTTOU,
	PosixSIGUSR1,
	PosixSIGUSR2,
	PosixSIGPOLL,
	PosixSIGPROF,
	PosixSIGSYS,
	PosixSIGTRAP,
	PosixSIGURG,
	PosixSIGVTALRM,
	PosixSIGXCPU,
	PosixSIGXFSZ,

	/* Optional Signals */
	PosixSIGIOT,
	PosixSIGEMT,
	PosixSIGSTKFLT,
	PosixSIGIO,
	PosixSIGCLD,
	PosixSIGPWR,
	PosixSIGINFO,
	PosixSIGLOST,
	PosixSIGWINCH,
	PosixSIGUNUSED,

	PosixNumberOfSignals
} PosixSignals;

/* Initialize libposix. Should call
 *
 *	libposix_define_errno to set the value of each PosixError
 *	libposix_define_signal to set the value of each PosixSignal
 *	libposix_translate_error to translate error strings to PosixError
 * 	libposix_set_signal_trampoline to dispatch signal received as notes
 *	libposix_set_stat_reader
 *	libposix_set_tms_reader
 *	libposix_set_timeval_reader
 *	libposix_set_timezone_reader
 */
typedef void (*PosixInit)(void);
extern void libposix_init(int argc, char *argv[], PosixInit init) __attribute__((noreturn));

/* Translate an error string to a PosixError, in the context of the
 * calling function (typically a pointer to a POSIX_* function).
 *
 * Must return 0 if unable to identify a PosixError.
 *
 * Translators are tried in reverse registration order.
 */
typedef PosixError (*PosixErrorTranslator)(char* error, uintptr_t caller);

/* Register a translation between Jehanne error strings and PosixErrors.
 * If caller is not zero, it is a pointer to the calling
 * functions to consider. Otherwise the translation will be tried whenever
 * for any caller, after the specialized ones.
 */
extern int libposix_translate_error(PosixErrorTranslator translation, uintptr_t caller);

/* define the value of a specific PosixError according to the library headers */
extern int libposix_define_errno(PosixError e, int errno);

/* Map a Dir to the stat structure expected by the library.
 *
 * Must return 0 on success or a PosixError on failure.
 */
typedef PosixError (*PosixStatReader)(void *statp, const Dir *dir);

extern int libposix_set_stat_reader(PosixStatReader reader);

/* Map a time provided by times() tms structure
 * expected by the library.
 *
 * Must return 0 on success or a PosixError on failure.
 */
typedef PosixError (*PosixTMSReader)(void *tms, 
	unsigned int proc_userms, unsigned int proc_sysms,
	unsigned int children_userms, unsigned int children_sysms);

extern int libposix_set_tms_reader(PosixTMSReader reader);

/* Map a time provided by gmtime() or localtime() to a timeval
 * expected by the library.
 *
 * Must return 0 on success or a PosixError on failure.
 */
typedef PosixError (*PosixTimevalReader)(void *timeval, const Tm *time);

extern int libposix_set_timeval_reader(PosixTimevalReader reader);

/* Map a time provided by gmtime() or localtime() to a timezone
 * expected by the library.
 *
 * Must return 0 on success or a PosixError on failure.
 */
typedef PosixError (*PosixTimezoneReader)(void *timezone, const Tm *time);

extern int libposix_set_timezone_reader(PosixTimezoneReader reader);

/* Map the POSIX_open flag and mode to Jehanne's arguments to open
 * or create.
 *
 * omode is a pointer to the open/create omode argument
 * cperm is a pointer to the create perm argument that must be filled
 * only if O_CREATE has been set in mode.
 *
 * Must return 0 on success or a PosixError on failure.
 */
typedef PosixError (*PosixOpenTranslator)(int flag, int mode, long *omode, long *cperm);

extern int libposix_translate_open(PosixOpenTranslator translation);

extern int libposix_translate_seek_whence(int seek_set, int seek_cur, int seek_end);

/* Map the main exit status received by POSIX_exit to the exit status
 * string for Jehanne.
 *
 * Must return nil if unable to translate the status, so that the
 * default translation strategy will be adopted.
 */
typedef char* (*PosixExitStatusTranslator)(int status);

extern int libposix_translate_exit_status(PosixExitStatusTranslator translator);

/* Dispatch the signal to the registered handlers.
 */
typedef enum PosixSignalAction
{
	SignalCatched = 1,
	SignalIgnored,
	SignalError,
	SignalDefault
} PosixSignalAction;
typedef PosixSignalAction (*PosixSignalTrampoline)(int signal);

extern int libposix_set_signal_trampoline(PosixSignalTrampoline trampoline);

extern int libposix_define_signal(PosixSignals signal, int code);

extern int libposix_define_realtime_signals(int sigrtmin, int sigrtmax);


/* Enable SIGCHLD emulation
 */
extern int libposix_emulate_SIGCHLD(void);

/* Define of WCONTINUED, WNOHANG and WUNTRACED bit flags.
 *
 * Note that WCONTINUED and WUNTRACED are not yet supported by libposix
 * and thus defining them cause an error.
 */
int libposix_set_wait_options(int wcontinued, int wnohang, int wuntraced);

#endif /* _LIBPOSIX_H */
