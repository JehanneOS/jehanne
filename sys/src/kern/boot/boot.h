/* Copyright (C) Charles Forsyth
 * See /doc/license/NOTICE.Plan9-9k.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */

typedef struct Method	Method;
struct Method
{
	char	*name;
	void	(*config)(Method*);
	int	(*connect)(void);
	char	*arg;
};
enum
{
	Statsz=	256,
	Nbarg=	16,
};
typedef struct BootBind	BootBind;
struct BootBind
{
	char	*name;
	char	*old;
	int	flag;
};

extern BootBind bootbinds[];
extern char screenconsolePath[];
extern char comconsolePath[];
extern char usbrcPath[];
extern char hjfsPath[];
extern char rcPath[];
extern char rcmainPath[];
extern char factotumPath[];
extern char fdiskPath[];
extern char prepPath[];
extern char ipconfigPath[];

extern void	authentication(int);
extern char*	bootdisk;
extern char*	rootdir;
extern int	(*cfs)(int);
extern int	cpuflag;
extern char	cputype[];
extern int	fflag;
extern int	kflag;
extern Method	method[];
extern void	(*pword)(int, Method*);
extern char	sys[];
extern unsigned char	hostkey[];
extern unsigned char	statbuf[Statsz];
extern int	bargc;
extern char	*bargv[Nbarg];

extern void	shell(char *c, char *d);
extern void	savelogs(void);

/* libc equivalent */
extern int	cache(int);
extern char*	checkkey(Method*, char*, char*);
extern void	fatal(char*) __attribute__ ((noreturn));
extern void	getpasswd(char*, int);
extern void	key(int, Method*);
extern int	outin(char*, char*, int);
extern int	plumb(char*, char*, int*, char*);
extern int	readfile(char*, char*, int);
extern int32_t	jehanne_readn(int, void*, int32_t);
extern void	run(char *file, ...);
extern int	sendmsg(int, char*);
extern void	setenv(char*, char*);
extern void	settime(int, int, char*);
extern void	srvcreate(char*, int);
extern void	warning(char*);
extern int	writefile(char*, char*, int);
extern void	boot(int, char **);
extern void	doauthenticate(int, Method*);
extern int		old9p(int);
extern int	parsefields(char*, char**, int, char*);

/* methods */
extern void	configtcp(Method*);
extern int	connecttcp(void);

extern void	configlocal(Method*);
extern int	connectlocal(void);

extern void	configsac(Method*);
extern int	connectsac(void);

extern void	configpaq(Method*);
extern int	connectpaq(void);

extern void	configembed(Method*);
extern int	connectembed(void);

extern void	configip(int, char**, int);

extern void	configrc(Method*);
extern int	connectrc(void);
/* hack for passing authentication address */
extern char	*authaddr;
