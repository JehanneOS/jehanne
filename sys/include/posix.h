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
 * Defining _LIBPOSIX_H before the include allow you to just get
 * data structure definition.
 */

#ifndef _LIBPOSIX_DEF
#define _LIBPOSIX_DEF

struct timespec
{
	long	tv_sec;
	long	tv_nsec;
};

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

/* getrusage who */
typedef enum PosixRUsages
{
	PosixRUsageSelf = 0,
	PosixRUsageChildren = 1,

	PosixRUsageUnknown = -1
} PosixRUsages;

/* errno values */
#define _ERRNO_H	// skip the Posix part, we just need the enum
#include <apw/errno.h>

/* signals */
typedef unsigned long PosixSignalMask;


typedef enum PosixSigProcMaskAction
{
	PosixSPMSetMask	= 0,
	PosixSPMBlock	= 1,
	PosixSPMUnblock	= 2
} PosixSigProcMaskAction;

#define PosixNumberOfSignals (sizeof(PosixSignalMask)*7)
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
	PosixSIGPWR,
	PosixSIGINFO,
	PosixSIGLOST,
	PosixSIGWINCH,
	PosixSIGUNUSED,

	PosixSIGRTMIN,
	PosixSIGRTMAX = PosixNumberOfSignals
} PosixSignals;

typedef enum PosixSigActionFlags
{
	/* supported flags */
	PosixSAFSigInfo		= 1<<0,
	PosixSAFRestart		= 1<<1,
	PosixSAFNoChildrenStop	= 1<<2,
	PosixSAFResetHandler	= 1<<3,
	PosixSAFNoChildrenWait	= 1<<4,

	/* ignored flags */
	PosixSAFNoDefer		= 1<<16,	/* notes are not reentrant */
	PosixSAFOnStack		= 1<<17,
	PosixSAFDisable		= 1<<18
} PosixSigActionFlags;

typedef enum PosixSigInfoCodes
{
	PosixSIUser = 1,
	PosixSIQueue,
	PosixSITimer,
	PosixSIAsyncIO,
	PosixSIMsgQueued,

	PosixSIFaultMapError,
	PosixSIFaultAccessError,

	PosixSIChildExited,
	PosixSIChildKilled,
	PosixSIChildDumped,
	PosixSIChildTrapped,
	PosixSIChildStopped,
	PosixSIChildContinued
} PosixSigInfoCodes;

union sigval {
	int		sival_int;	/* Integer signal value */
	void*		sival_ptr;	/* Pointer signal value */
	void*		_si_addr;	/* Address of faulting address */
	int		_si_status;	/* Child exit status */
	uintptr_t	_sival_raw;	/* Raw value */
};

struct sigevent {
	int		sigev_notify;               /* Notification type */
	int		sigev_signo;                /* Signal number */
	union sigval	sigev_value;                /* Signal value */
	void		(*sigev_notify_function)( union sigval );
                                               /* Notification function */
	long		*sigev_notify_attributes;    /* Notification Attributes */
};

typedef struct {
	int		si_signo;	/* Signal number */
	int		si_code;	/* Cause of the signal */
	int		si_errno;
	int		si_pid;		/* Pid of sender */
	int		si_uid;		/* Uid of sender */
	union sigval	si_value;	/* Signal value */
} PosixSignalInfo;

#define si_addr si_value._si_addr
#define si_status si_value._si_status

typedef void (*PosixSigHandler)(int);
typedef void (*PosixSigAction)(int, PosixSignalInfo *, void * );

struct sigaction {
	int		sa_flags;       /* Special flags to affect behavior of signal */
	PosixSignalMask	sa_mask;        /* Additional set of signals to be blocked */
					/*   during execution of signal-catching */
					/*   function. */
	union {
		PosixSigHandler	_handler;	/* SIG_DFL, SIG_IGN, or pointer to a function */
		PosixSigAction	_sigaction;
	} _signal_handlers;
};

#define sa_handler    _signal_handlers._handler
#define sa_sigaction  _signal_handlers._sigaction

typedef enum PosixFDCmds
{
	PosixFDCDupFD = 1,
	PosixFDCDupFDCloseOnExec,
	PosixFDCGetFD,
	PosixFDCSetFD,
	PosixFDCGetFL,
	PosixFDCSetFL
} PosixFDCmds;

/* https://pubs.opengroup.org/onlinepubs/9699919799/functions/sysconf.html
 */
typedef enum PosixSysConfNames
{
	/* Posix 1 */
	PosixSCNArgMax = 1,	// _SC_ARG_MAX
	PosixSCNChildMax,	// _SC_CHILD_MAX
	PosixSCNHostNameMax,	// _SC_HOST_NAME_MAX
	PosixSCNLoginNameMax,	// _SC_LOGIN_NAME_MAX
	PosixSCNClockTicks,	// _SC_CLK_TCK
	PosixSCNOpenMax,	// _SC_OPEN_MAX
	PosixSCNPageSize,	// _SC_PAGESIZE
	PosixSCNPosixVersion,	// _SC_VERSION

	/* Posix 2 */
	PosixSCNLineMax		// _SC_LINE_MAX
} PosixSysConfNames;

#endif /* _LIBPOSIX_DEF */

#ifndef _LIBPOSIX_H
#define _LIBPOSIX_H

typedef unsigned long clock_t;

#define __POSIX_EXIT_PREFIX "posix error "
#define __POSIX_EXIT_SIGNAL_PREFIX "terminated by posix signal "
#define __POSIX_SIGNAL_PREFIX "posix: "

extern unsigned int POSIX_alarm(int *errnop, unsigned int seconds);
extern int POSIX_access(int *errnop, const char *path, int amode);
extern int POSIX_dup(int *errnop, int fildes);
extern int POSIX_dup2(int *errnop, int fildes, int fildes2);
extern void POSIX_exit(int code) __attribute__((noreturn));
extern int POSIX_chmod(int *errnop, const char *path, int mode);
extern int POSIX_fchmodat(int *errnop, int fd, const char *path, long mode, int flag);
extern int POSIX_chown(int *errnop, const char *pathname, int owner, int group);
extern int POSIX_fchownat(int *errnop, int fd, const char *path, int owner, int group, int flag);
extern int POSIX_lchown(int *errnop, const char *path, int owner, int group);
extern int POSIX_chdir(int *errnop, const char *path);
extern int POSIX_fchdir(int *errnop, int fd);
extern int POSIX_mkdir(int *errnop, const char *path, int mode);
extern int POSIX_close(int *errnop, int file);
extern int POSIX_execve(int *errnop, const char *name, char * const*argv, char * const*env);
extern int POSIX_fork(int *errnop);
extern int POSIX_getrusage(int *errnop, PosixRUsages who, void *r_usage);
extern char* POSIX_getcwd(int *errnop, char *buf, int size);
extern char* POSIX_getlogin(int *errnop);
extern int POSIX_getlogin_r(int *errnop, char *name, int namesize);
extern char* POSIX_getpass(int *errnop, const char *prompt);
extern int POSIX_getpid(int *errnop);
extern int POSIX_getppid(int *errnop);
extern int POSIX_isatty(int *errnop, int file);
extern int POSIX_kill(int *errnop, int pid, int sig);
extern int POSIX_link(int *errnop, const char *existingPath, const char *newPath);
extern off_t POSIX_lseek(int *errnop, int fd, off_t pos, int whence);
extern int POSIX_open(int *errnop, const char *name, int flags, int mode);
extern long POSIX_pread(int *errnop, int fd, char *buf, size_t len, long offset);
extern long POSIX_pwrite(int *errnop, int fd, const char *buf, size_t len, long offset);
extern long POSIX_read(int *errnop, int fd, char *buf, size_t len);
extern int POSIX_stat(int *errnop, const char *file, void *stat);
extern int POSIX_fstat(int *errnop, int file, void *stat);
extern int POSIX_lstat(int *errnop, const char *file, void *stat);
extern clock_t POSIX_times(int *errnop, void *tms);
extern int POSIX_unlink(int *errnop, const char *name);
extern int POSIX_wait(int *errnop, int *status);
extern int POSIX_waitpid(int *errnop, int pid, int *status, int options);
extern long POSIX_write(int *errnop, int fd, const void *buf, size_t len);
extern int POSIX_gettimeofday(int *errnop, void *timeval, void *timezone);
extern char* POSIX_getenv(int *errnop, const char *name);
extern int POSIX_setenv(int *errno, const char *name, const char *value, int overwrite);
extern int POSIX_unsetenv(int *errnop, const char *name);
extern void *POSIX_sbrk(int *errnop, ptrdiff_t incr);
extern void * POSIX_malloc(int *errnop, size_t size);
extern void *POSIX_realloc(int *errnop, void *ptr, size_t size);
extern void *POSIX_calloc(int *errnop, size_t nelem, size_t size);
extern void POSIX_free(void *ptr);
extern unsigned int POSIX_sleep(unsigned int seconds);
extern int POSIX_pipe(int *errnop, int fildes[2]);
extern int POSIX_umask(int *errnop, int mask);
extern int POSIX_fcntl(int *errnop, int fd, PosixFDCmds cmd, uintptr_t arg);
extern long POSIX_sysconf(int *errnop, PosixSysConfNames name);
extern int POSIX_rmdir(int *errnop, const char *name);

extern int POSIX_sigaddset(int *errnop, PosixSignalMask *set, int signo);
extern int POSIX_sigdelset(int *errnop, PosixSignalMask *set, int signo);
extern int POSIX_sigismember(int *errnop, const PosixSignalMask *set, int signo);
extern int POSIX_sigfillset(int *errnop, PosixSignalMask *set);
extern int POSIX_sigemptyset(int *errnop, PosixSignalMask *set);
extern int POSIX_sigprocmask(int *errnop, PosixSigProcMaskAction how, const PosixSignalMask *set, PosixSignalMask *oset);
extern int POSIX_sigpending(int *errnop, PosixSignalMask *set);
extern int POSIX_sigsuspend(int *errnop, const PosixSignalMask *mask);
extern int POSIX_sigaction(int *errnop, int signo, const struct sigaction *act, struct sigaction *old);
extern int POSIX_sigtimedwait(int *errnop, const PosixSignalMask *set, PosixSignalInfo *info, const struct timespec *timeout);
extern int POSIX_sigqueue(int *errnop, int pid, int signo, const union sigval value);

extern int POSIX_getuid(int *errnop);
extern int POSIX_geteuid(int *errnop);
extern int POSIX_setuid(int *errnop, int uid);
extern int POSIX_seteuid(int *errnop, int euid);
extern int POSIX_setreuid(int *errnop, int ruid, int euid);
extern int POSIX_getgid(int *errnop);
extern int POSIX_getegid(int *errnop);
extern int POSIX_setgid(int *errnop, int gid);
extern int POSIX_setegid(int *errnop, int egid);
extern int POSIX_setregid(int *errnop, int rgid, int egid);
extern int POSIX_getpgrp(int *errnop);
extern int POSIX_getpgid(int *errnop, int pid);
extern int POSIX_setpgid(int *errnop, int pid, int pgid);
extern int POSIX_getsid(int *errnop, int pid);
extern int POSIX_setsid(int *errnop);

extern int POSIX_tcgetpgrp(int *errnop, int fd);
extern int POSIX_tcsetpgrp(int *errnop, int fd, int pgrp);

extern int libposix_getdents(int *errnop, int fd, char *buf, int buf_bytes);
extern PosixError libposix_translate_kernel_errors(const char *msg);

/* Library initialization
 */

/* Initialize libposix. Should call
 *
 *	libposix_define_errno to set the value of each PosixError
 *	libposix_define_at_fdcwd to set the value of AT_FDCWD (for fchmodat)
 *	libposix_translate_error to translate error strings to PosixError
 *	libposix_set_stat_reader
 *	libposix_set_tms_reader
 *	libposix_set_timeval_reader
 *	libposix_set_timezone_reader
 */
typedef void (*PosixInit)(int argc, char *argv[]);
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

/* define the value of AT_FDCWD according to the library headers */
extern int libposix_define_at_fdcwd(int AT_FDCWD);

/* define the value of O_NONBLOCK according to the library headers */
extern int libposix_define_ononblock(int O_NONBLOCK);

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
 * or create. It's also used in chmod to translate mode.
 *
 * omode is a pointer to the open/create omode argument
 * cperm is a pointer to the create perm argument that must be filled
 * only if omode is nil or O_CREATE has been set in mode.
 *
 * Must return 0 on success or a PosixError on failure.
 *
 * In flag:
 * - O_CLOEXEC must be mapped to OCEXEC in omode
 * - O_TRUNC must be mapped to OTRUNC in omode
 * - O_APPEND|O_CREAT must be mapped to DMAPPEND in cperm
 * - O_APPEND without O_CREAT must return PosixENOTSUP
 * - O_EXCL|O_CREAT must be mapped to DMEXCL in cperm
 * - O_EXCL without O_CREAT it must return PosixENOTSUP
 * - O_SEARCH must be mapped to OEXEC|DMDIR in omode
 * - O_DIRECTORY|O_CREAT must return PosixEINVAL
 * - O_DIRECTORY without O_CREAT must be mapped to DMDIR in omode
 * - O_DSYNC, O_NOCTTY, O_NOFOLLOW, O_NONBLOCK, O_RSYNC, O_SYNC
 *   and O_TTY_INIT must be ignored
 * - S_ISUID, S_ISGID, S_ISVTX must be ignored
 */
typedef PosixError (*PosixOpenTranslator)(int flag, int mode, long *omode, long *cperm);

extern int libposix_translate_open(PosixOpenTranslator translation);

extern int libposix_translate_seek_whence(int seek_set, int seek_cur, int seek_end);

extern int libposix_translate_access_mode(int f_ok, int r_ok, int w_ok, int x_ok);

/* Map the main exit status received by POSIX_exit to the exit status
 * string for Jehanne.
 *
 * Must return nil if unable to translate the status, so that the
 * default translation strategy will be adopted.
 */
typedef char* (*PosixExitStatusTranslator)(int status);

extern int libposix_translate_exit_status(PosixExitStatusTranslator translator);

/* Execute process disposition (executed when main returns)
 */
typedef void (*PosixProcessDisposer)(int status);

extern int libposix_on_process_disposition(PosixProcessDisposer dispose);

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
