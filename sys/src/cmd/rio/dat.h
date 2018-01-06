/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* Portions of this file are Copyright (C) 2015-2018 Giacomo Tesio <giacomo@tesio.it>
 * See /doc/license/gpl-2.0.txt for details about the licensing.
 */
/* Portions of this file are Copyright (C) 9front's team.
 * See /doc/license/9front-mit for details about the licensing.
 * See http://code.9front.org/hg/plan9front/ for a list of authors.
 */
enum
{
	Qdir,			/* /dev for this window */
	Qcons,
	Qconsctl,
	Qcursor,
	Qwdir,
	Qwinid,
	Qwinname,
	Qlabel,
	Qkbd,
	Qmouse,
	Qnew,
	Qscreen,
	Qsnarf,
	Qtext,
	Qwctl,
	Qwindow,
	Qwsys,		/* directory of window directories */
	Qwsysdir,		/* window directory, child of wsys */

	QMAX,
};

#define	STACK	8192
#define	MAXSNARF	100*1024

typedef	struct	Consreadmesg Consreadmesg;
typedef	struct	Conswritemesg Conswritemesg;
typedef struct	Kbdreadmesg Kbdreadmesg;
typedef	struct	Stringpair Stringpair;
typedef	struct	Dirtab Dirtab;
typedef	struct	Fid Fid;
typedef	struct	Filsys Filsys;
typedef	struct	Mouseinfo	Mouseinfo;
typedef	struct	Mousereadmesg Mousereadmesg;
typedef	struct	Mousestate	Mousestate;
typedef	struct	Ref Ref;
typedef	struct	Timer Timer;
typedef	struct	Wctlmesg Wctlmesg;
typedef	struct	Window Window;
typedef	struct	Xfid Xfid;

enum
{
	Selborder		= 4,		/* border of selected window */
	Unselborder	= 1,		/* border of unselected window */
	Scrollwid 		= 12,		/* width of scroll bar */
	Scrollgap 		= 4,		/* gap right of scroll bar */
	BIG			= 3,		/* factor by which window dimension can exceed screen */
	TRUE		= 1,
	FALSE		= 0,
};

#define	QID(w,q)	((w<<8)|(q))
#define	WIN(q)	((((uint32_t)(q).path)>>8) & 0xFFFFFF)
#define	FILE(q)	(((uint32_t)(q).path) & 0xFF)

enum	/* control messages */
{
	Wakeup,
	Reshaped,
	Topped,
	Repaint,
	Refresh,
	Movemouse,
	Rawon,
	Rawoff,
	Holdon,
	Holdoff,
	Deleted,
	Exited,
};

struct Wctlmesg
{
	int		type;
	Rectangle	r;
	void		*p;
};

struct Conswritemesg
{
	Channel	*cw;		/* chan(Stringpair) */
};

struct Consreadmesg
{
	Channel	*c1;		/* chan(tuple(char*, int) == Stringpair) */
	Channel	*c2;		/* chan(tuple(char*, int) == Stringpair) */
};

struct Mousereadmesg
{
	Channel	*cm;		/* chan(Mouse) */
};

struct Stringpair	/* rune and nrune or byte and nbyte */
{
	void		*s;
	int		ns;
};

struct Mousestate
{
	Mouse;
	uint32_t	counter;	/* serial no. of mouse event */
};

struct Mouseinfo
{
	Mousestate	queue[16];
	int	ri;	/* read index into queue */
	int	wi;	/* write index */
	uint32_t	counter;	/* serial no. of last mouse event we received */
	uint32_t	lastcounter;	/* serial no. of last mouse event sent to client */
	int	lastb;	/* last button state we received */
	uint8_t	qfull;	/* filled the queue; no more recording until client comes back */	
};	

struct Window
{
	Ref	ref;
	QLock	ql;
	Frame	frame;
	Image		*i;		/* window image, nil when deleted */
	Mousectl	mc;
	Mouseinfo	mouse;
	Channel		*ck;		/* chan(char*) */
	Channel		*cctl;		/* chan(Wctlmesg)[4] */
	Channel		*conswrite;	/* chan(Conswritemesg) */
	Channel		*consread;	/* chan(Consreadmesg) */
	Channel		*mouseread;	/* chan(Mousereadmesg) */
	Channel		*wctlread;	/* chan(Consreadmesg) */
	Channel		*kbdread;	/* chan(Consreadmesg) */
	Channel		*complete;	/* chan(Completion*) */
	Channel		*gone;		/* chan(char*) */
	uint32_t			nr;			/* number of runes in window */
	uint32_t			maxr;		/* number of runes allocated in r */
	Rune			*r;
	uint32_t			nraw;
	Rune			*raw;
	uint32_t			org;
	uint32_t			q0;
	uint32_t			q1;
	uint32_t			qh;
	int			id;
	char			name[32];
	uint32_t			namecount;
	Rectangle		scrollr;
	/*
	 * Rio once used originwindow, so screenr could be different from i->r.
	 * Now they're always the same but the code doesn't assume so.
	*/
	Rectangle		screenr;	/* screen coordinates of window */
	int			resized;
	int			wctlready;
	Rectangle		lastsr;
	int			topped;
	int			notefd;
	uint8_t		scrolling;
	Cursor		cursor;
	Cursor		*cursorp;
	uint8_t		holding;
	uint8_t		rawing;
	uint8_t		ctlopen;
	uint8_t		wctlopen;
	uint8_t		deleted;
	uint8_t		mouseopen;
	uint8_t		kbdopen;
	char			*label;
	char			*dir;
};

void		winctl(void*);
void		winshell(void*);
Window*	wlookid(int);
Window*	wmk(Image*, Mousectl*, Channel*, Channel*, int);
Window*	wpointto(Point);
Window*	wtop(Point);
void		wtopme(Window*);
void		wbottomme(Window*);
char*	wcontents(Window*, int*);
int		wbswidth(Window*, Rune);
int		wclickmatch(Window*, int, int, int, uint32_t*);
int		wclose(Window*);
int		wctlmesg(Window*, int, Rectangle, void*);
uint32_t		wbacknl(Window*, uint32_t, uint32_t);
uint32_t		winsert(Window*, Rune*, int, uint32_t);
void		waddraw(Window*, Rune*, int);
void		wborder(Window*, int);
void		wclunk(Window*);
void		wclosewin(Window*);
void		wcurrent(Window*);
void		wcut(Window*);
void		wdelete(Window*, uint32_t, uint32_t);
void		wdoubleclick(Window*, uint32_t*, uint32_t*);
void		wfill(Window*);
void		wframescroll(Window*, int);
void		wkeyctl(Window*, Rune);
void		wmousectl(Window*);
void		wmovemouse(Window*, Point);
void		wpaste(Window*);
void		wplumb(Window*);
void		wlook(Window*);
void		wrefresh(Window*);
void		wrepaint(Window*);
void		wresize(Window*, Image*);
void		wscrdraw(Window*);
void		wscroll(Window*, int);
void		wselect(Window*);
void		wsendctlmesg(Window*, int, Rectangle, void*);
void		wsetcursor(Window*, int);
void		wsetname(Window*);
void		wsetorigin(Window*, uint32_t, int);
void		wsetpid(Window*, int, int);
void		wsetselect(Window*, uint32_t, uint32_t);
void		wshow(Window*, uint32_t);
void		wsnarf(Window*);
void 		wscrsleep(Window*, uint32_t);
void		wsetcols(Window*, int);

struct Dirtab
{
	char		*name;
	uint8_t	type;
	uint32_t		qid;
	uint32_t		perm;
};

struct Fid
{
	int		fid;
	int		busy;
	int		open;
	int		mode;
	Qid		qid;
	Window	*w;
	Dirtab	*dir;
	Fid		*next;
	int		nrpart;
	uint8_t	rpart[UTFmax];
};

struct Xfid
{
		Ref	ref;
		Xfid		*next;
		Xfid		*free;
		Fcall;
		Channel	*c;	/* chan(void(*)(Xfid*)) */
		Fid		*f;
		uint8_t	*buf;
		Filsys	*fs;
		int		flushtag;	/* our tag, so flush can find us */
		Channel	*flushc;	/* channel(int) to notify us we're being flushed */
};

Channel*	xfidinit(void);
void		xfidctl(void*);
void		xfidflush(Xfid*);
void		xfidattach(Xfid*);
void		xfidopen(Xfid*);
void		xfidclose(Xfid*);
void		xfidread(Xfid*);
void		xfidwrite(Xfid*);

enum
{
	Nhash	= 16,
};

struct Filsys
{
		int		cfd;
		int		sfd;
		int		pid;
		char		*user;
		Channel	*cxfidalloc;	/* chan(Xfid*) */
		Channel	*csyncflush;	/* chan(int) */
		Fid		*fids[Nhash];
};

Filsys*	filsysinit(Channel*);
int		filsysmount(Filsys*, int);
Xfid*		filsysrespond(Filsys*, Xfid*, Fcall*, char*);
void		filsyscancel(Xfid*);

void		wctlproc(void*);
void		wctlthread(void*);

void		deletetimeoutproc(void*);

struct Timer
{
	int		dt;
	int		cancel;
	Channel	*c;	/* chan(int) */
	Timer	*next;
};

Font		*font;
Mousectl	*mousectl;
Mouse	*mouse;
Display	*display;
Image	*view;
Screen	*wscreen;
Cursor	boxcursor;
Cursor	crosscursor;
Cursor	sightcursor;
Cursor	whitearrow;
Cursor	query;
Cursor	*corners[9];

Image	*background;
Image	*cols[NCOL];
Image	*titlecol;
Image	*lighttitlecol;
Image	*dholdcol;
Image	*holdcol;
Image	*lightholdcol;
Image	*paleholdcol;
Image	*paletextcol;
Image	*sizecol;
int	reverse;	/* there are no pastel paints in the dungeons and dragons world -- rob pike */

Window	**window;
Window	*wkeyboard;	/* window of simulated keyboard */
int		nwindow;
int		snarffd;
int		gotscreen;
int		servekbd;
Window	*input;
QLock	all;			/* BUG */
Filsys	*filsys;
Window	*hidden[100];
int		nhidden;
int		nsnarf;
Rune*	snarf;
int		scrolling;
int		maxtab;
Channel*	winclosechan;
Channel*	deletechan;
char		*startdir;
int		sweeping;
int		wctlfd;
char		srvpipe[64];
char		srvwctl[64];
int		errorshouldabort;
int		menuing;		/* menu action is pending; waiting for window to be indicated */
int		snarfversion;	/* updated each time it is written */
int		messagesize;		/* negotiated in 9P version setup */
int		shiftdown;
int		debug;
